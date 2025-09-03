#include "jit.common.h"
#include <math.h>

typedef struct _gaussian {
    t_object ob;
    long radius;
    float sigma;
    float *weight_cache;
    long cache_size;
} t_gaussian;

BEGIN_USING_C_LINKAGE
t_jit_err gaussian_init(void);
t_gaussian *gaussian_new(void);
void gaussian_free(t_gaussian *x);
t_jit_err gaussian_matrix_calc(t_gaussian *x, void *inputs, void *outputs);
t_jit_err gaussian_radius_set(t_gaussian *x, void *attr, long ac, t_atom *av);
t_jit_err gaussian_sigma_set(t_gaussian *x, void *attr, long ac, t_atom *av);
void gaussian_clear(t_gaussian *x);
void gaussian_precompute_weights(t_gaussian *x);
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
}

t_jit_err gaussian_sigma_set(t_gaussian *x, void *attr, long ac, t_atom *av){
    x->sigma = atom_getfloat(av);
    gaussian_precompute_weights(x);
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
    
    for (long gz = 0; gz < diameter; gz++) {
        for (long gy = 0; gy < diameter; gy++) {
            for (long gx = 0; gx < diameter; gx++) {
                float norm_x = ((float)gx / (diameter - 1)) * 2.0f - 1.0f;
                float norm_y = ((float)gy / (diameter - 1)) * 2.0f - 1.0f;
                float norm_z = ((float)gz / (diameter - 1)) * 2.0f - 1.0f;
                float weight = expf(-((norm_x * norm_x + norm_y * norm_y + norm_z * norm_z) / (2 * x->sigma * x->sigma)));
                x->weight_cache[idx++] = weight;
                total_weight += weight;
            }
        }
    }
    
    // Normalize weights so they sum to 1
    for (long i = 0; i < x->cache_size; i++) {
        x->weight_cache[i] /= total_weight;
    }
}

t_jit_err gaussian_matrix_calc(t_gaussian *x, void *inputs, void *outputs) {
    t_jit_err err = JIT_ERR_NONE;
    t_jit_matrix_info in_minfo;
    char *in_bp, *out_bp;
    long vox_x, vox_y, vox_z;
    t_jit_object *in_matrix, *out_matrix;
    long in_savelock, out_savelock;
    void *in_mdata, *out_mdata;
    float *fop;

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
        return JIT_ERR_INVALID_INPUT;
    }

    jit_object_method(out_matrix, _jit_sym_getdata, &out_mdata);

    if (!out_mdata) {
        return JIT_ERR_INVALID_OUTPUT;
    }

    in_bp = (char *)in_mdata;
    out_bp = (char *)out_mdata;
    
    // Process each output voxel
    for (vox_z = 0; vox_z < in_minfo.dim[2]; vox_z++) {
        for (vox_y = 0; vox_y < in_minfo.dim[1]; vox_y++) {
            for (vox_x = 0; vox_x < in_minfo.dim[0]; vox_x++) {
                long out_index = vox_x * in_minfo.dimstride[0] +
                                vox_y * in_minfo.dimstride[1] +
                                vox_z * in_minfo.dimstride[2];
                fop = (float *)(out_bp + out_index);
                fop[0] = convolve(x, in_bp, in_minfo.dim, in_minfo.dimstride,
                                          vox_x, vox_y, vox_z);
            }
        }
    }

    jit_object_method(in_matrix, _jit_sym_lock, in_savelock);
    jit_object_method(out_matrix, _jit_sym_lock, out_savelock);
    return err;
}

float convolve(t_gaussian *x, char *in_bp, long *dim, long *stride,
                        long gx, long gy, long gz) {
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
                    total_weight += weight;
                }
            }
        }
    }
    
    return (total_weight > 0.0f) ? sum / total_weight : 0.0f;
}
