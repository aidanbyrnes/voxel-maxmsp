#include "jit.common.h"
#include <math.h>
#include <pthread.h>
#include <unistd.h>

typedef struct _gaussian {
    t_object ob;
    long radius;
    float sigma;
    float *weight_cache;
    long cache_size;
    long num_threads;
} t_gaussian;

typedef struct _thread_data {
    t_gaussian *x;
    char *in_bp;
    char *out_bp;
    long *dim;
    long *stride;
    long start_slice;
    long end_slice;
} t_thread_data;

BEGIN_USING_C_LINKAGE
t_jit_err gaussian_init(void);
t_gaussian *gaussian_new(void);
void gaussian_free(t_gaussian *x);
t_jit_err gaussian_matrix_calc(t_gaussian *x, void *inputs, void *outputs);
t_jit_err gaussian_radius_set(t_gaussian *x, void *attr, long ac, t_atom *av);
t_jit_err gaussian_sigma_set(t_gaussian *x, void *attr, long ac, t_atom *av);
void gaussian_clear(t_gaussian *x);
void gaussian_precompute_weights(t_gaussian *x);
void *gaussian_thread_worker(void *arg);
float convolve(t_gaussian *x, char *in_bp, long *dim, long *stride, long gx, long gy, long gz);
END_USING_C_LINKAGE

static void *_gaussian_class = NULL;

t_jit_err gaussian_init(void) {
    long attrflags = JIT_ATTR_GET_DEFER_LOW | JIT_ATTR_SET_USURP_LOW;
    t_jit_object *attr;
    t_jit_object *mop;

    _gaussian_class = jit_class_new("gaussian", (method)gaussian_new, (method)gaussian_free, sizeof(t_gaussian), 0L);

    mop = jit_object_new(_jit_sym_jit_mop, 1, 1);
    jit_class_addadornment(_gaussian_class, mop);

    // methods
    jit_class_addmethod(_gaussian_class, (method)gaussian_matrix_calc, "matrix_calc", A_CANT, 0L);

    // attributes
    attr = jit_object_new(_jit_sym_jit_attr_offset, "radius", _jit_sym_long, attrflags,
                          (method)NULL, (method)gaussian_radius_set, calcoffset(t_gaussian, radius));
    jit_class_addattr(_gaussian_class, attr);
    CLASS_ATTR_LABEL(_gaussian_class, "radius", 0, "Radius");
    
    attr = jit_object_new(_jit_sym_jit_attr_offset, "sigma", _jit_sym_float32, attrflags,
                          (method)NULL, (method)gaussian_sigma_set, calcoffset(t_gaussian, sigma));
    jit_class_addattr(_gaussian_class, attr);
    CLASS_ATTR_LABEL(_gaussian_class, "sigma", 0, "Standard Deviation");

    jit_class_register(_gaussian_class);

    return JIT_ERR_NONE;
}

t_gaussian *gaussian_new(void) {
    t_gaussian *x;

    if ((x = (t_gaussian *)jit_object_alloc(_gaussian_class))) {
        x->radius = 1;
        x->sigma = 1.0f;
        x->weight_cache = NULL;
        x->cache_size = 0;
        x->num_threads = sysconf(_SC_NPROCESSORS_ONLN);
        gaussian_precompute_weights(x);
    } else {
        x = NULL;
    }

    return x;
}

void gaussian_free(t_gaussian *x) {
    if (x->weight_cache) {
        free(x->weight_cache);
    }
}

t_jit_err gaussian_radius_set(t_gaussian *x, void *attr, long ac, t_atom *av){
    x->radius = atom_getlong(av);
    gaussian_precompute_weights(x);
    return JIT_ERR_NONE;
}

t_jit_err gaussian_sigma_set(t_gaussian *x, void *attr, long ac, t_atom *av){
    x->sigma = atom_getfloat(av);
    gaussian_precompute_weights(x);
    return JIT_ERR_NONE;
}

void gaussian_precompute_weights(t_gaussian *x) {
    if (x->weight_cache) {
        free(x->weight_cache);
    }
    
    long diameter = x->radius * 2 + 1;
    x->cache_size = diameter * diameter * diameter;
    x->weight_cache = (float *)malloc(x->cache_size * sizeof(float));
    
    float total_weight = 0.0f;
    long idx = 0;
    float sigma_sq_2 = 2.0f * x->sigma * x->sigma;
    
    for (long gz = 0; gz < diameter; gz++) {
        for (long gy = 0; gy < diameter; gy++) {
            for (long gx = 0; gx < diameter; gx++) {
                float norm_x = ((float)gx / (diameter - 1)) * 2.0f - 1.0f;
                float norm_y = ((float)gy / (diameter - 1)) * 2.0f - 1.0f;
                float norm_z = ((float)gz / (diameter - 1)) * 2.0f - 1.0f;
                
                float dist_sq = norm_x * norm_x + norm_y * norm_y + norm_z * norm_z;
                float weight = expf(-dist_sq / sigma_sq_2);
                
                x->weight_cache[idx++] = weight;
                total_weight += weight;
            }
        }
    }
    
    // Normalize weights so they sum to 1
    if (total_weight > 0.0f) {
        for (long i = 0; i < x->cache_size; i++) {
            x->weight_cache[i] /= total_weight;
        }
    }
}

void *gaussian_thread_worker(void *arg) {
    t_thread_data *data = (t_thread_data *)arg;
    t_gaussian *x = data->x;
    char *in_bp = data->in_bp;
    char *out_bp = data->out_bp;
    long *dim = data->dim;
    long *stride = data->stride;
    
    for (long vox_z = data->start_slice; vox_z <= data->end_slice; vox_z++) {
        for (long vox_y = 0; vox_y < dim[1]; vox_y++) {
            for (long vox_x = 0; vox_x < dim[0]; vox_x++) {
                long out_index = vox_x * stride[0] + vox_y * stride[1] + vox_z * stride[2];
                float *fop = (float *)(out_bp + out_index);
                fop[0] = convolve(x, in_bp, dim, stride, vox_x, vox_y, vox_z);
            }
        }
    }
    
    return NULL;
}

t_jit_err gaussian_matrix_calc(t_gaussian *x, void *inputs, void *outputs) {
    t_jit_err err = JIT_ERR_NONE;
    t_jit_matrix_info in_minfo;
    char *in_bp, *out_bp;
    t_jit_object *in_matrix, *out_matrix;
    long in_savelock, out_savelock;
    void *in_mdata, *out_mdata;

    in_matrix = jit_object_method(inputs, _jit_sym_getindex, 0);
    out_matrix = jit_object_method(outputs, _jit_sym_getindex, 0);

    if (!in_matrix || !out_matrix) {
        return JIT_ERR_INVALID_INPUT;
    }
    
    in_savelock = (long)jit_object_method(inputs, _jit_sym_lock, 1);
    out_savelock = (long)jit_object_method(outputs, _jit_sym_lock, 1);

    jit_object_method(in_matrix, _jit_sym_getinfo, &in_minfo);
    jit_object_method(in_matrix, _jit_sym_getdata, &in_mdata);

    if (!in_mdata) {
        err = JIT_ERR_INVALID_INPUT;
        goto out;
    }

    jit_object_method(out_matrix, _jit_sym_getdata, &out_mdata);

    if (!out_mdata) {
        err = JIT_ERR_INVALID_OUTPUT;
        goto out;
    }

    in_bp = (char *)in_mdata;
    out_bp = (char *)out_mdata;

    if (x->num_threads > 1) {
        pthread_t *threads = (pthread_t *)malloc(x->num_threads * sizeof(pthread_t));
        t_thread_data *thread_data = (t_thread_data *)malloc(x->num_threads * sizeof(t_thread_data));
        
        if (!threads || !thread_data) {
            err = JIT_ERR_OUT_OF_MEM;
            if (threads) free(threads);
            if (thread_data) free(thread_data);
            goto out;
        }
        
        long slices_per_thread = in_minfo.dim[2] / x->num_threads;
        long remaining_slices = in_minfo.dim[2] % x->num_threads;
        
        for (long i = 0; i < x->num_threads; i++) {
            thread_data[i].x = x;
            thread_data[i].in_bp = in_bp;
            thread_data[i].out_bp = out_bp;
            thread_data[i].dim = in_minfo.dim;
            thread_data[i].stride = in_minfo.dimstride;
            thread_data[i].start_slice = i * slices_per_thread;
            thread_data[i].end_slice = thread_data[i].start_slice + slices_per_thread - 1;
            
            // distribute remaining slices to first threads
            if (i < remaining_slices) {
                thread_data[i].end_slice++;
                // adjust start positions for subsequent threads
                for (long j = i + 1; j < x->num_threads; j++) {
                    thread_data[j].start_slice++;
                    thread_data[j].end_slice++;
                }
            }
            
            if (pthread_create(&threads[i], NULL, gaussian_thread_worker, &thread_data[i]) != 0) {
                // fallback to single-threaded processing
                err = JIT_ERR_GENERIC;
                for (long j = 0; j < i; j++) {
                    pthread_join(threads[j], NULL);
                }
                free(threads);
                free(thread_data);
                goto single_threaded_fallback;
            }
        }
        
        // wait for all threads
        for (long i = 0; i < x->num_threads; i++) {
            pthread_join(threads[i], NULL);
        }
        
        free(threads);
        free(thread_data);
    } else {
        for (long vox_z = 0; vox_z < in_minfo.dim[2]; vox_z++) {
            for (long vox_y = 0; vox_y < in_minfo.dim[1]; vox_y++) {
                for (long vox_x = 0; vox_x < in_minfo.dim[0]; vox_x++) {
                    long index = vox_x * in_minfo.dimstride[0] + vox_y * in_minfo.dimstride[1] + vox_z * in_minfo.dimstride[2];
                    float *fop = (float *)(out_bp + index);
                    fop[0] = convolve(x, in_bp, in_minfo.dim, in_minfo.dimstride,
                                      vox_x, vox_y, vox_z);
                }
            }
        }
    }
    
    goto out;

single_threaded_fallback:
    for (long vox_z = 0; vox_z < in_minfo.dim[2]; vox_z++) {
        for (long vox_y = 0; vox_y < in_minfo.dim[1]; vox_y++) {
            for (long vox_x = 0; vox_x < in_minfo.dim[0]; vox_x++) {
                long index = vox_x * in_minfo.dimstride[0] + vox_y * in_minfo.dimstride[1] + vox_z * in_minfo.dimstride[2];
                float *fop = (float *)(out_bp + index);
                fop[0] = convolve(x, in_bp, in_minfo.dim, in_minfo.dimstride, vox_x, vox_y, vox_z);
            }
        }
    }
    err = JIT_ERR_NONE;

out:
    jit_object_method(in_matrix, _jit_sym_lock, in_savelock);
    jit_object_method(out_matrix, _jit_sym_lock, out_savelock);
    return err;
}

float convolve(t_gaussian *x, char *in_bp, long *dim, long *stride, long gx, long gy, long gz) {
    float sum = 0.0f;
    float total_weight = 0.0f;
    long diameter = x->radius * 2 + 1;
    long weight_idx = 0;
    
    // Calculate bounds
    long z_start = (gz >= x->radius) ? gz - x->radius : 0;
    long z_end = (gz + x->radius < dim[2]) ? gz + x->radius : dim[2] - 1;
    long y_start = (gy >= x->radius) ? gy - x->radius : 0;
    long y_end = (gy + x->radius < dim[1]) ? gy + x->radius : dim[1] - 1;
    long x_start = (gx >= x->radius) ? gx - x->radius : 0;
    long x_end = (gx + x->radius < dim[0]) ? gx + x->radius : dim[0] - 1;
    
    for (long vox_z = gz - x->radius; vox_z <= gz + x->radius; vox_z++) {
        for (long vox_y = gy - x->radius; vox_y <= gy + x->radius; vox_y++) {
            for (long vox_x = gx - x->radius; vox_x <= gx + x->radius; vox_x++) {
                float weight = x->weight_cache[weight_idx++];
                
                if (vox_x >= x_start && vox_x <= x_end &&
                    vox_y >= y_start && vox_y <= y_end &&
                    vox_z >= z_start && vox_z <= z_end) {
                    long index = vox_x * stride[0] + vox_y * stride[1] + vox_z * stride[2];
                    float *fip = (float *)(in_bp + index);
                    sum += fip[0] * weight;
                }
            }
        }
    }
    return sum;
}
