#include "jit.common.h"

typedef struct _vertexarray {
    t_object ob;
    long size;
} t_vertexarray;

BEGIN_USING_C_LINKAGE
t_jit_err vertexarray_init(void);
t_vertexarray *vertexarray_new(void);
void vertexarray_free(t_vertexarray *x);
t_jit_err vertexarray_matrix_calc(t_vertexarray *x, void *inputs, void *outputs);
void vertexarray_clear(t_vertexarray *x);
void indexToXYZ(int index, int *x, int *y, int *z, int sizeX, int sizeY);
END_USING_C_LINKAGE

static void *_vertexarray_class = NULL;

t_jit_err vertexarray_init(void) {
    long attrflags = JIT_ATTR_GET_DEFER_LOW | JIT_ATTR_SET_USURP_LOW;
    t_jit_object *attr;
    t_jit_object *mop;

    _vertexarray_class = jit_class_new("vertexarray", (method)vertexarray_new, (method)vertexarray_free, sizeof(t_vertexarray), 0L);

    mop = jit_object_new(_jit_sym_jit_mop, 1, 1);
    jit_mop_output_nolink(mop, 1);
    jit_class_addadornment(_vertexarray_class, mop);

    // methods
    jit_class_addmethod(_vertexarray_class, (method)vertexarray_matrix_calc, "matrix_calc", A_CANT, 0L);

    // attributes
    attr = jit_object_new(_jit_sym_jit_attr_offset, "size", _jit_sym_long, attrflags,
                          (method)NULL, (method)NULL, calcoffset(t_vertexarray, size));
    jit_class_addattr(_vertexarray_class, attr);
    CLASS_ATTR_LABEL(_vertexarray_class, "size", 0, "Grid size");

    jit_class_register(_vertexarray_class);

    return JIT_ERR_NONE;
}

t_vertexarray *vertexarray_new(void) {
    t_vertexarray *x;

    if ((x = (t_vertexarray *)jit_object_alloc(_vertexarray_class))) {
        x->size = 1;
    } else {
        x = NULL;
    }

    return x;
}

void vertexarray_free(t_vertexarray *x) {
}

t_jit_err vertexarray_matrix_calc(t_vertexarray *x, void *inputs, void *outputs) {
    t_jit_err err = JIT_ERR_NONE;
    t_jit_matrix_info in_minfo, out_minfo;
    char *in_bp, *out_bp;
    long i, j, k, index;
    t_jit_object *in_matrix, *out_matrix;
    long in_savelock, out_savelock;
    void *in_mdata, *out_mdata;
    float *fip, *fop;
    int vox_x, vox_y, vox_z;

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

    int p_count = 4;

    out_minfo.type = _jit_sym_float32;
    out_minfo.dimcount = 1;
    out_minfo.dim[0] = in_minfo.dim[0] * in_minfo.dim[1] * in_minfo.dim[2];
    out_minfo.planecount = p_count;
    out_minfo.flags = 0;

    jit_object_method(out_matrix, _jit_sym_setinfo, &out_minfo);
    jit_object_method(out_matrix, _jit_sym_getdata, &out_mdata);

    if (!out_mdata) {
        return JIT_ERR_INVALID_OUTPUT;
    }

    in_bp = (char *)in_mdata;
    out_bp = (char *)out_mdata;

    fop = (float *)out_bp;
    
    index = 0;
    for(vox_z = 0; vox_z < in_minfo.dim[2]; vox_z++){
        for(vox_y = 0; vox_y < in_minfo.dim[1]; vox_y++){
            for(vox_x = 0; vox_x < in_minfo.dim[0]; vox_x++){
                fip = (float *)(in_bp + (vox_x * in_minfo.dimstride[0] + vox_y * in_minfo.dimstride[1] + vox_z * in_minfo.dimstride[2]));
                float weight = fip[0];
                
                if(weight > 0){
                    fop[index] = (float)vox_x / in_minfo.dim[0] + 1.0f / in_minfo.dim[0] * .5f;
                    fop[index + 1] = (float)vox_y / in_minfo.dim[1] + 1.0f / in_minfo.dim[1] * .5f;
                    fop[index + 2] = (float)vox_z / in_minfo.dim[2] + 1.0f / in_minfo.dim[2] * .5f;
                    fop[index + 3] = weight;
                }
                else{
                    fop[index] = 0;
                    fop[index + 1] = 0;
                    fop[index + 2] = 0;
                    fop[index + 3] = 0;
                }
                
                index += p_count;
            }
        }
    }
out:
    jit_object_method(in_matrix, _jit_sym_lock, in_savelock);
    jit_object_method(out_matrix, _jit_sym_lock, out_savelock);
    return err;
}
