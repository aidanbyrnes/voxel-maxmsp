#include "jit.common.h"
#include "max.jit.mop.h"

typedef struct _max_centroid {
    t_object ob;
    void *obex;
    void *meanout;
    t_atom *av;
} t_max_centroid;

BEGIN_USING_C_LINKAGE
t_jit_err centroid_init(void);
void * max_centroid_new(t_symbol *s, long argc, t_atom *argv);
void max_centroid_free(t_max_centroid *x);
void max_centroid_assist(t_max_centroid *x, void *b, long m, long a, char *s);
void max_centroid_bang(t_max_centroid *x);
void max_centroid_mproc(t_max_centroid *x, void *mop);
END_USING_C_LINKAGE

static void *max_centroid_class = NULL;

void ext_main(void *r) {
    t_class *max_class, *jit_class;

    centroid_init();

    max_class = class_new("voxel.centroid", (method)max_centroid_new,
                          (method)max_centroid_free, sizeof(t_max_centroid),
                          NULL, A_GIMME, 0);
    max_jit_class_obex_setup(max_class, calcoffset(t_max_centroid, obex));
    jit_class = jit_class_findbyname(gensym("centroid"));
    max_jit_class_mop_wrap(max_class, jit_class,
                           MAX_JIT_MOP_FLAGS_OWN_BANG |
                           MAX_JIT_MOP_FLAGS_OWN_OUTPUTMATRIX);
    max_jit_classex_mop_mproc(max_class, jit_class, max_centroid_mproc);
    max_jit_class_wrap_standard(max_class, jit_class, 0);

    class_addmethod(max_class, (method)max_centroid_assist, "assist", A_CANT,
                    0);
    class_addmethod(max_class, (method)max_centroid_bang, "bang");

    class_register(CLASS_BOX, max_class);
    max_centroid_class = max_class;
}

void max_centroid_bang(t_max_centroid *x) {
    long ac;
    void *o;

    if (max_jit_mop_getoutputmode(x) && x->av) {
        o = max_jit_obex_jitob_get(x);
        jit_object_method(o, gensym("getmean"), &ac, &(x->av));
        outlet_anything(x->meanout, _jit_sym_list, ac, x->av);
    }
}

void max_centroid_mproc(t_max_centroid *x, void *mop) {
    t_jit_err err;

    if (mop) {
        if ((err = (t_jit_err)jit_object_method(
                 max_jit_obex_jitob_get(x), _jit_sym_matrix_calc,
                 jit_object_method(mop, _jit_sym_getinputlist),
                 jit_object_method(mop, _jit_sym_getoutputlist)))) {
            jit_error_code(x, err);
        } else {
            max_centroid_bang(x);
        }
    }
}

void max_centroid_assist(t_max_centroid *x, void *b, long m, long a, char *s) {
    if (m == 1) { // input
        max_jit_mop_assist(x, b, m, a, s);
    } else { // output
        switch (a) {
            case 0:
                sprintf(s, "(list) position");
                break;

            case 1:
                sprintf(s, "dumpout");
                break;
        }
    }
}

void max_centroid_free(t_max_centroid *x) {
    max_jit_mop_free(x);
    jit_object_free(max_jit_obex_jitob_get(x));

    if (x->av) {
        jit_freebytes(x->av, sizeof(t_atom) * JIT_MATRIX_MAX_PLANECOUNT);
    }

    max_jit_object_free(x);
}

void * max_centroid_new(t_symbol *s, long argc, t_atom *argv) {
    t_max_centroid *x;
    void *o;

    x = (t_max_centroid *)max_jit_object_alloc(max_centroid_class,
                                               gensym("centroid"));

    if (x) {
        x->av = NULL;
        o = jit_object_new(gensym("centroid"));

        if (o) {
            max_jit_mop_setup_simple(x, o, argc, argv);
            x->meanout = outlet_new(x, 0L);
            x->av = jit_getbytes(sizeof(t_atom) * JIT_MATRIX_MAX_PLANECOUNT);
            max_jit_attr_args(x, argc, argv);
        } else {
            jit_object_error((t_object *)x,
                             "voxel.centroid: could not allocate object");
            object_free((t_object *)x);
            x = NULL;
        }
    }

    return (x);
}
