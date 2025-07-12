#include "jit.common.h"
#include "max.jit.mop.h"

typedef struct _max_pcloud2grid {
    t_object ob;
    void *obex;
} t_max_pcloud2grid;

BEGIN_USING_C_LINKAGE
t_jit_err pcloud2grid_init(void);
void * max_pcloud2grid_new(t_symbol *s, long argc, t_atom *argv);
void max_pcloud2grid_free(t_max_pcloud2grid *x);
void max_jit_freenect2_assist(t_max_pcloud2grid *x, void *b, long msg, long arg, char *s);
END_USING_C_LINKAGE

static void *max_pcloud2grid_class = NULL;

void ext_main(void *r) {
    t_class *max_class, *jit_class;

    pcloud2grid_init();

    max_class = class_new("voxel.pcloud2grid", (method)max_pcloud2grid_new, (method)max_pcloud2grid_free, sizeof(t_max_pcloud2grid), NULL, A_GIMME, 0);
    max_jit_class_obex_setup(max_class, calcoffset(t_max_pcloud2grid, obex));

    jit_class = jit_class_findbyname(gensym("pcloud2grid"));
    max_jit_class_mop_wrap(max_class, jit_class, 0);
    max_jit_class_wrap_standard(max_class, jit_class, 0);

    class_addmethod(max_class, (method)max_jit_mop_assist, "assist", A_CANT, 0);

    class_register(CLASS_BOX, max_class);
    max_pcloud2grid_class = max_class;
}

/************************************************************************************/
// Object Life Cycle

void * max_pcloud2grid_new(t_symbol *s, long argc, t_atom *argv) {
    t_max_pcloud2grid *x;
    void *o;

    x = (t_max_pcloud2grid *)max_jit_object_alloc(max_pcloud2grid_class, gensym("pcloud2grid"));

    if (x) {
        o = jit_object_new(gensym("pcloud2grid"));

        if (o) {
            max_jit_mop_setup_simple(x, o, argc, argv);
            max_jit_attr_args(x, argc, argv);
        } else {
            jit_object_error((t_object *)x, "voxel.pcloud2grid: could not allocate object");
            object_free((t_object *)x);
            x = NULL;
        }
    }

    return (x);
}

void max_pcloud2grid_free(t_max_pcloud2grid *x) {
    max_jit_mop_free(x);
    jit_object_free(max_jit_obex_jitob_get(x));
    max_jit_object_free(x);
}

void max_jit_freenect2_assist(t_max_pcloud2grid *x, void *b, long msg, long arg, char *s) {
    if (msg == ASSIST_OUTLET) {
        switch (arg) {
            case 0:
                sprintf(s, "(matrix) voxel grid");
                break;

            default:
                sprintf(s, "dumpout");
                break;
        }
    }
}
