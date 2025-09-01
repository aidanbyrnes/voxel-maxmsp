#include "jit.common.h"

typedef struct _pcloud2grid {
    t_object ob;
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
    jit_class_addadornment(_pcloud2grid_class, mop);
    jit_mop_single_type(mop, gensym("float32"));
    jit_mop_single_planecount(mop, 1);
    jit_attr_setlong(mop, _jit_sym_adapt, 0);

    // methods
    jit_class_addmethod(_pcloud2grid_class, (method)pcloud2grid_matrix_calc, "matrix_calc", A_CANT, 0L);
    jit_class_addmethod(_pcloud2grid_class, (method)pcloud2grid_clear, "clear", 0L); // Add the clear method

    // attributes
    attr = jit_object_new(_jit_sym_jit_attr_offset, "autoclear", _jit_sym_long, attrflags,
                          (method)NULL, (method)NULL, calcoffset(t_pcloud2grid, autoclear));
    jit_class_addattr(_pcloud2grid_class, attr);
    CLASS_ATTR_LABEL(_pcloud2grid_class, "autoclear", 0, "Auto Clear Output");
    CLASS_ATTR_STYLE(_pcloud2grid_class, "autoclear", 0, "onoff");

    jit_class_register(_pcloud2grid_class);

    return JIT_ERR_NONE;
}

t_pcloud2grid *pcloud2grid_new(void) {
    t_pcloud2grid *x;

    if ((x = (t_pcloud2grid *)jit_object_alloc(_pcloud2grid_class))) {
        x->autoclear = 1;
        x->out_matrix = NULL;
    } else {
        x = NULL;
    }

    return x;
}

void pcloud2grid_free(t_pcloud2grid *x) {
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
            size = out_minfo.dim[0] * out_minfo.dim[1] * out_minfo.dim[2];
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
    long in_savelock, out_savelock;
    char *in_bp, *out_bp;
    long i, j, k, index;
    t_jit_object *in_matrix;
    void *in_mdata, *out_mdata;
    float *fip, *fop;

    in_matrix = jit_object_method(inputs, _jit_sym_getindex, 0);
    x->out_matrix = jit_object_method(outputs, _jit_sym_getindex, 0);

    if (!in_matrix || !x->out_matrix) {
        return JIT_ERR_INVALID_INPUT;
    }
    
    in_savelock = (long)jit_object_method(inputs, _jit_sym_lock, 1);
    out_savelock = (long)jit_object_method(outputs, _jit_sym_lock, 1);

    jit_object_method(in_matrix, _jit_sym_getinfo, &in_minfo);
    jit_object_method(in_matrix, _jit_sym_getdata, &in_mdata);

    if (!in_mdata) {
        return JIT_ERR_INVALID_INPUT;
    }
    
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

    if (in_minfo.dimcount == 2 && in_minfo.planecount >= 3) {
        for (j = 0; j < in_minfo.dim[0]; j++) {
            for (i = 0; i < in_minfo.dim[1]; i++) {
                fip = (float *)(in_bp + (j * in_minfo.dimstride[1]) + (i * in_minfo.dimstride[0]));

                long grid_x = (long)(fip[0] * out_minfo.dim[0]);
                long grid_y = (long)(fip[1] * out_minfo.dim[1]);
                long grid_z = (long)(fip[2] * out_minfo.dim[2]);

                grid_x = MAX(0, MIN(grid_x, out_minfo.dim[0] - 1));
                grid_y = MAX(0, MIN(grid_y, out_minfo.dim[1] - 1));
                grid_z = MAX(0, MIN(grid_z, out_minfo.dim[2] - 1));
                
                index = grid_x + grid_y * out_minfo.dim[0] + grid_z * out_minfo.dim[0] * out_minfo.dim[1];

                if (index >= 0 && index < (out_minfo.dim[0] * out_minfo.dim[1] * out_minfo.dim[2])) {
                    fop[index] = 1;
                }
            }
        }
    }
out:
    jit_object_method(in_matrix, _jit_sym_lock, in_savelock);
    jit_object_method(x->out_matrix, _jit_sym_lock, out_savelock);
    return err;
}
