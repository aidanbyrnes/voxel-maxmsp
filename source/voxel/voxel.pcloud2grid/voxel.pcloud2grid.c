#include "jit.common.h"

typedef struct _pcloud2grid {
    t_object ob;
    long size;
    long normalize_out;
    long autoclear;
    void *out_matrix;
} t_pcloud2grid;

BEGIN_USING_C_LINKAGE
t_jit_err pcloud2grid_init(void);
t_pcloud2grid *pcloud2grid_new(void);
void pcloud2grid_free(t_pcloud2grid *x);
t_jit_err pcloud2grid_matrix_calc(t_pcloud2grid *x, void *inputs, void *outputs);
void pcloud2grid_clear(t_pcloud2grid *x);
END_USING_C_LINKAGE

static void *_pcloud2grid_class = NULL;

t_jit_err pcloud2grid_init(void) {
    long attrflags = JIT_ATTR_GET_DEFER_LOW | JIT_ATTR_SET_USURP_LOW;
    t_jit_object *attr;
    t_jit_object *mop;

    _pcloud2grid_class = jit_class_new("pcloud2grid", (method)pcloud2grid_new, (method)pcloud2grid_free, sizeof(t_pcloud2grid), 0L);

    mop = jit_object_new(_jit_sym_jit_mop, 1, 1);
    jit_mop_output_nolink(mop, 1);
    jit_class_addadornment(_pcloud2grid_class, mop);

    // methods
    jit_class_addmethod(_pcloud2grid_class, (method)pcloud2grid_matrix_calc, "matrix_calc", A_CANT, 0L);
    jit_class_addmethod(_pcloud2grid_class, (method)pcloud2grid_clear, "clear", 0L); // Add the clear method

    // attributes
    attr = jit_object_new(_jit_sym_jit_attr_offset, "grid_size", _jit_sym_long, attrflags,
                          (method)NULL, (method)NULL, calcoffset(t_pcloud2grid, size));
    jit_class_addattr(_pcloud2grid_class, attr);
    CLASS_ATTR_LABEL(_pcloud2grid_class, "grid_size", 0, "Grid size");

    attr = jit_object_new(_jit_sym_jit_attr_offset, "normalize_out", _jit_sym_long, attrflags,
                          (method)NULL, (method)NULL, calcoffset(t_pcloud2grid, normalize_out));
    jit_class_addattr(_pcloud2grid_class, attr);
    CLASS_ATTR_LABEL(_pcloud2grid_class, "normalize_out", 0, "Normalize output");
    CLASS_ATTR_STYLE(_pcloud2grid_class, "normalize_out", 0, "onoff");

    attr = jit_object_new(_jit_sym_jit_attr_offset, "autoclear", _jit_sym_long, attrflags,
                          (method)NULL, (method)NULL, calcoffset(t_pcloud2grid, autoclear));
    jit_class_addattr(_pcloud2grid_class, attr);
    CLASS_ATTR_LABEL(_pcloud2grid_class, "autoclear", 0, "Auto clear output");
    CLASS_ATTR_STYLE(_pcloud2grid_class, "autoclear", 0, "onoff");

    jit_class_register(_pcloud2grid_class);

    return JIT_ERR_NONE;
}

t_pcloud2grid *pcloud2grid_new(void) {
    t_pcloud2grid *x;

    if ((x = (t_pcloud2grid *)jit_object_alloc(_pcloud2grid_class))) {
        x->size = 1;
        x->normalize_out = 1;
        x->autoclear = 1;
        x->out_matrix = NULL;
    } else {
        x = NULL;
    }

    return x;
}

void pcloud2grid_free(t_pcloud2grid *x) {
    jit_object_free(x->out_matrix);
}

void pcloud2grid_clear(t_pcloud2grid *x) {
    if (x->out_matrix) {
        t_jit_matrix_info out_minfo;
        char *out_bp;
        float *fop;
        long i, size;

        jit_object_method(x->out_matrix, _jit_sym_getinfo, &out_minfo);
        jit_object_method(x->out_matrix, _jit_sym_getdata, &out_bp);

        if (out_bp) {
            size = out_minfo.dim[0] * out_minfo.planecount;
            fop = (float *)out_bp;
            for (i = 0; i < size; i++) {
                fop[i] = 0.0f;
            }
        }
    }
}

t_jit_err pcloud2grid_matrix_calc(t_pcloud2grid *x, void *inputs, void *outputs) {
    t_jit_err err = JIT_ERR_NONE;
    t_jit_matrix_info in_minfo, out_minfo;
    char *in_bp, *out_bp;
    long i, j, k, index;
    t_jit_object *in_matrix;
    void *in_mdata, *out_mdata;
    float *fip, *fop;
    long in_dimcount, in_planecount, in_dim[JIT_MATRIX_MAX_DIMCOUNT];

    in_matrix = jit_object_method(inputs, _jit_sym_getindex, 0);
    x->out_matrix = jit_object_method(outputs, _jit_sym_getindex, 0);

    if (!in_matrix || !x->out_matrix) {
        return JIT_ERR_INVALID_INPUT;
    }

    jit_object_method(in_matrix, _jit_sym_getinfo, &in_minfo);
    jit_object_method(in_matrix, _jit_sym_getdata, &in_mdata);

    if (!in_mdata) {
        return JIT_ERR_INVALID_INPUT;
    }

    in_dimcount = in_minfo.dimcount;
    in_planecount = in_minfo.planecount;

    for (i = 0; i < in_dimcount; i++) {
        in_dim[i] = in_minfo.dim[i];
    }

    long out_size = x->size * x->size * x->size;
    int p_count = 3;

    out_minfo.type = _jit_sym_float32;
    out_minfo.dimcount = 1;
    out_minfo.dim[0] = out_size;
    out_minfo.planecount = p_count;
    out_minfo.flags = 0;

    jit_object_method(x->out_matrix, _jit_sym_setinfo, &out_minfo);
    jit_object_method(x->out_matrix, _jit_sym_getinfo, &out_minfo);
    jit_object_method(x->out_matrix, _jit_sym_getdata, &out_mdata);

    if (!out_mdata) {
        return JIT_ERR_INVALID_OUTPUT;
    }

    in_bp = (char *)in_mdata;
    out_bp = (char *)out_mdata;

    if (x->autoclear) {
        pcloud2grid_clear(x);
    }

    fop = (float *)out_bp;

    if (in_dimcount >= 2 && in_planecount >= 3) {
        long width = in_dim[0];
        long height = in_dim[1];

        for (j = 0; j < height; j++) {
            for (i = 0; i < width; i++) {
                fip = (float *)(in_bp + (j * in_minfo.dimstride[1]) + (i * in_minfo.dimstride[0]));

                float grid_x = (int)(fip[0] * x->size);
                float grid_y = (int)(fip[1] * x->size);
                float grid_z = (int)(fip[2] * x->size);

                grid_x = MAX(0, MIN(grid_x, x->size - 1));
                grid_y = MAX(0, MIN(grid_y, x->size - 1));
                grid_z = MAX(0, MIN(grid_z, x->size - 1));

                index = grid_z * (x->size * x->size) + grid_y * x->size + grid_x;

                if (x->normalize_out) {
                    grid_x /= (float)x->size;
                    grid_y /= (float)x->size;
                    grid_z /= (float)x->size;
                }

                if (index >= 0 && index < out_size) {
                    fop[index * p_count] = grid_x;
                    fop[index * p_count + 1] = grid_y;
                    fop[index * p_count + 2] = grid_z;
                }
            }
        }
    }

    return err;
}
