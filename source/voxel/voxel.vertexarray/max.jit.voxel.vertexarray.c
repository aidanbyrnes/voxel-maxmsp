#include "jit.common.h"
#include "max.jit.mop.h"

typedef struct _max_vertexarray {
    t_object ob;
    void *obex;
} t_max_vertexarray;

BEGIN_USING_C_LINKAGE
t_jit_err vertexarray_init(void);
void * max_vertexarray_new(t_symbol *s, long argc, t_atom *argv);
void max_vertexarray_free(t_max_vertexarray *x);
void max_jit_freenect2_assist(t_max_vertexarray *x, void *b, long msg, long arg, char *s);
END_USING_C_LINKAGE

static void *max_vertexarray_class = NULL;

void ext_main(void *r) {
    t_class *max_class, *jit_class;

    vertexarray_init();

    max_class = class_new("voxel.vertexarray", (method)max_vertexarray_new, (method)max_vertexarray_free, sizeof(t_max_vertexarray), NULL, A_GIMME, 0);
    max_jit_class_obex_setup(max_class, calcoffset(t_max_vertexarray, obex));

    jit_class = jit_class_findbyname(gensym("vertexarray"));
    max_jit_class_mop_wrap(max_class, jit_class,  MAX_JIT_MOP_FLAGS_OWN_ADAPT | MAX_JIT_MOP_FLAGS_OWN_OUTPUTMODE);
    max_jit_class_wrap_standard(max_class, jit_class, 0);

    class_addmethod(max_class, (method)max_jit_mop_assist, "assist", A_CANT, 0);

    class_register(CLASS_BOX, max_class);
    max_vertexarray_class = max_class;
}

/************************************************************************************/
// Object Life Cycle

void * max_vertexarray_new(t_symbol *s, long argc, t_atom *argv) {
    t_max_vertexarray *x;
    void *o;

    x = (t_max_vertexarray *)max_jit_object_alloc(max_vertexarray_class, gensym("vertexarray"));

    if (x) {
        o = jit_object_new(gensym("vertexarray"));

        if (o) {
            max_jit_obex_jitob_set(x,o);
            max_jit_obex_dumpout_set(x,outlet_new(x,NULL));
            max_jit_mop_setup(x);
            max_jit_mop_inputs(x);
            max_jit_mop_outputs(x);
        } else {
            jit_object_error((t_object *)x, "voxel.vertexarray: could not allocate object");
            object_free((t_object *)x);
            x = NULL;
        }
    }

    return (x);
}

void max_vertexarray_free(t_max_vertexarray *x) {
    max_jit_mop_free(x);
    jit_object_free(max_jit_obex_jitob_get(x));
    max_jit_object_free(x);
}

void max_jit_freenect2_assist(t_max_vertexarray *x, void *b, long msg, long arg, char *s) {
    if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0:
                sprintf(s, "(matrix) vertex array");
                break;

            default:
                sprintf(s, "dumpout");
                break;
        }
    }
}
