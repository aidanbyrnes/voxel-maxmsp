#include "jit.common.h"

typedef struct _centroid {
    t_object ob;
    float mean[3];
} t_centroid;

BEGIN_USING_C_LINKAGE
t_jit_err centroid_init(void);
t_centroid *centroid_new(void);
void centroid_free(t_centroid *x);
t_jit_err centroid_matrix_calc(t_centroid *x, void *inputs, void *outputs);
void indexToXYZ(int index, int *x, int *y, int *z, int sizeX, int sizeY);
END_USING_C_LINKAGE

static void *_centroid_class = NULL;

t_jit_err centroid_init(void) {
    long attrflags = JIT_ATTR_SET_OPAQUE_USER | JIT_ATTR_GET_DEFER_LOW;
    t_jit_object *attr;
    t_jit_object *mop;

    _centroid_class = jit_class_new("centroid", (method)centroid_new, (method)centroid_free, sizeof(t_centroid), 0L);

    mop = jit_object_new(_jit_sym_jit_mop, 1, 0);
    jit_class_addadornment(_centroid_class, mop);

    // methods
    jit_class_addmethod(_centroid_class, (method)centroid_matrix_calc, "matrix_calc", A_CANT, 0L);

    // attributes
    attr = jit_object_new(_jit_sym_jit_attr_offset_array, "mean", _jit_sym_float32, 3, attrflags,
        (method)0L, (method)0L, 0, calcoffset(t_centroid, mean));
    jit_class_addattr(_centroid_class, attr);

    jit_class_register(_centroid_class);

    return JIT_ERR_NONE;
}

t_centroid *centroid_new(void) {
    t_centroid *x;

    if ((x = (t_centroid *)jit_object_alloc(_centroid_class))) {
        // Initialize mean values to 0
        x->mean[0] = 0.0f;
        x->mean[1] = 0.0f;
        x->mean[2] = 0.0f;
    } else {
        x = NULL;
    }

    return x;
}

void centroid_free(t_centroid *x) {
}

t_jit_err centroid_matrix_calc(t_centroid *x, void *inputs, void *outputs) {
    t_jit_err err = JIT_ERR_NONE;
    t_jit_matrix_info in_minfo;
    char *in_bp;
    t_jit_object *in_matrix;
    void *in_mdata;
    float *fip;
    long in_dimcount, in_planecount, in_length;
    int vox_x, vox_y, vox_z;
    int size;
    float weight;
    float samples = 0;

    in_matrix = jit_object_method(inputs, _jit_sym_getindex, 0);

    if (!in_matrix) {
        return JIT_ERR_INVALID_INPUT;
    }

    jit_object_method(in_matrix, _jit_sym_getinfo, &in_minfo);
    jit_object_method(in_matrix, _jit_sym_getdata, &in_mdata);

    if (!in_mdata) {
        return JIT_ERR_INVALID_INPUT;
    }
    
    in_dimcount = in_minfo.dimcount;
    in_planecount = in_minfo.planecount;
    in_length = in_minfo.dim[0];
    
    if(in_minfo.type != _jit_sym_float32){ return err; }

    // Reset mean values to 0
    for(int j = 0; j < 3; j++){
        x->mean[j] = 0.0f;
    }
    
    //Get length per side assuming cubic grid
    size = cbrt(in_length);
    
    in_bp = (char *)in_mdata;

    if (in_dimcount == 1 && in_planecount >= 1) {
        if(in_planecount < 4){
            for (int i = 0; i < in_length; i++) {
                fip = (float *)(in_bp + (i * in_minfo.dimstride[0]));
                
                weight = fip[0];
                if(weight <= 0){ continue; }
                
                indexToXYZ(i, &vox_x, &vox_y, &vox_z, size, size);
                
                x->mean[0] += (float)vox_x * weight / (size - 1);
                x->mean[1] += (float)vox_y * weight / (size - 1);
                x->mean[2] += (float)vox_z * weight / (size - 1);
                
                samples += weight;
            }
        }
        else{
            for (int i = 0; i < in_length; i++) {
                fip = (float *)(in_bp + (i * in_minfo.dimstride[0]));
                
                weight = fip[3];
                if(weight <= 0){ continue; }
                
                x->mean[0] += fip[0] * weight;
                x->mean[1] += fip[1] * weight;
                x->mean[2] += fip[2] * weight;
                
                samples += weight;
            }
        }
        
        if (samples > 0) {
            for(int j = 0; j < 3; j++){
                x->mean[j] /= samples;
            }
        }	
    }

    return err;
}

void indexToXYZ(int index, int *x, int *y, int *z, int sizeX, int sizeY) {
    *z = index / (sizeX * sizeY);
    int remainder = index % (sizeX * sizeY);
    *y = remainder / sizeX;
    *x = remainder % sizeX;
}
