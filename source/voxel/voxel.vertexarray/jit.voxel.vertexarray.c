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
    void *in_mdata, *out_mdata;
    float *fip, *fop;
    long in_dimcount, in_planecount, in_dim[JIT_MATRIX_MAX_DIMCOUNT];
    long in_size;
    int vox_x, vox_y, vox_z;
    int grid_size;

    in_matrix = jit_object_method(inputs, _jit_sym_getindex, 0);
    out_matrix = jit_object_method(outputs, _jit_sym_getindex, 0);

    if (!in_matrix || !out_matrix) {
        return JIT_ERR_INVALID_INPUT;
    }

    jit_object_method(in_matrix, _jit_sym_getinfo, &in_minfo);
    jit_object_method(in_matrix, _jit_sym_getdata, &in_mdata);

    if (!in_mdata) {
        return JIT_ERR_INVALID_INPUT;
    }

    in_dimcount = in_minfo.dimcount;
    in_planecount = in_minfo.planecount;
    in_size = in_minfo.dim[0];

    for (i = 0; i < in_dimcount; i++) {
        in_dim[i] = in_minfo.dim[i];
    }

    int p_count = 4;

    out_minfo.type = _jit_sym_float32;
    out_minfo.dimcount = 1;
    out_minfo.dim[0] = in_size;
    out_minfo.planecount = p_count;
    out_minfo.flags = 0;

    jit_object_method(out_matrix, _jit_sym_setinfo, &out_minfo);
    jit_object_method(out_matrix, _jit_sym_getdata, &out_mdata);

    if (!out_mdata) {
        return JIT_ERR_INVALID_OUTPUT;
    }

    in_bp = (char *)in_mdata;
    out_bp = (char *)out_mdata;
    
    grid_size = cbrt(in_size);

    fop = (float *)out_bp;
    
    if (in_dimcount == 1 && in_planecount >= 1) {
        for (int i = 0; i < in_size; i++) {
            fip = (float *)(in_bp + (i * in_minfo.dimstride[0]));
            
            float weight = fip[0];
            if(weight > 0){
                indexToXYZ(i, &vox_x, &vox_y, &vox_z, grid_size, grid_size);
                fop[i * p_count] = (float)vox_x / (grid_size - 1);
                fop[i * p_count + 1] = (float)vox_y / (grid_size - 1);
                fop[i * p_count + 2] = (float)vox_z / (grid_size - 1);
                fop[i * p_count + 3] = weight;
            }
            else{
                fop[i * p_count] = 0;
                fop[i * p_count + 1] = 0;
                fop[i * p_count + 2] = 0;
                fop[i * p_count + 3] = 0;
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
