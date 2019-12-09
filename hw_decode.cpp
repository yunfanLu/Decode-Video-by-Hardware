#include "hw_decode.hpp"
#include <stdio.h>
#include <Python.h>
#include "numpy/arrayobject.h"
#include <math.h>
#include <omp.h>
#include <time.h>

extern "C" {
    const char* dt = "cuda";
    HWDecoder vid_decoder(dt);
 
    int wrap_get_num_gops(const char* filename, int* num_gops) {
        return vid_decoder.get_num_gops(filename, num_gops);
    }
    
    int wrap_get_gop_frame(const char* filename, int gop_target) {
        return vid_decoder.get_gop_frame(filename, gop_target);
    }    
    
    int wrap_get_gop_frames(const char* filename, int* gop_targets, int numframes) {
        return vid_decoder.get_gop_frames(filename, gop_targets, numframes);
    }
    int wrap_load_gop_frames(const char* filename, int* gop_targets, int numframes, link_queue q_frames) {
        return vid_decoder.load_gop_frames(filename, gop_targets, numframes, q_frames);
    }
    
    void q_frames_traverse(link_queue q, PyArrayObject ** rgb_arr, void(*visit)(link_queue q, PyArrayObject ** rgb_arr))
    {
        visit(q, rgb_arr);
    }

    void node_visit(link_queue q, PyArrayObject ** rgb_arr) {
        /* 头结点 */
        link_node p = q -> front -> next;
        if(!p)
        {
            printf("队列为空");
        }
        int num = queue_len(q);
        int frame_idx = 0;
        char filename[1024];
        uint8_t *dest;
        int width, height, wrap, linesize, frame_dim;
        while(p)
        {
            if (frame_idx == 0) {
                width = p->data.width;
                height = p->data.height;
                wrap = p->data.wrap;
                linesize = width*3;
                frame_dim = height*linesize;
                // printf("width: %d, height: %d, wrap: %d\n", width, height, wrap);
                npy_intp dims[4];
                dims[0] = num;
                dims[1] = height;
                dims[2] = width;
                dims[3] = 3;
                *rgb_arr = (PyArrayObject*)PyArray_ZEROS(4, dims, NPY_UINT8, 0);
                dest = (uint8_t*) (*rgb_arr)->data;
                //*rgb_arr = PyArray
            }
            uint8_t *src = p->data.data;
            memcpy(dest + frame_idx * frame_dim, src, height * linesize * sizeof(uint8_t));            
            free(p->data.data);
            /*
            FILE *fp;
            snprintf(filename, sizeof(filename), "hello-mp4_%d.pgm", frame_idx);
            if ((fp = fopen(filename, "wb")) == NULL) {
                printf("open file %s failed!\n", filename);
                return;
            }

            fprintf(fp, "P6\n%d %d\n%d\n", width, height, 255);
            for (int y = 0; y < height; y++)
                fwrite(p->data.data + y * wrap, 1, linesize, fp);
             
            fclose(fp);
            */
            frame_idx ++;
            
            p = p -> next;
        }
    }
    
    int decode_video(const char* filename, int frames, PyArrayObject ** rgb_arr) {
        clock_t start = clock();
        clock_t end;
        int* gop_targets = NULL;
#if 0   
        int gop_count; 
        int ret = wrap_get_num_gops(filename, &gop_count);
        if (ret < 0)
            return -1;
        end = clock();
        start = clock();
        /* compute gop targets */
        frames = frames < gop_count ? frames : gop_count;
        gop_targets = new int[frames];
        float seg_size = float(gop_count) / frames;
        for (int i=0; i<frames; ++i) {
            gop_targets[i] = floor(i*seg_size);
        }           
#endif        
        link_queue q_frames = queue_init();
        wrap_load_gop_frames(filename, gop_targets, frames, q_frames);
        if (gop_targets != NULL)
	        delete[] gop_targets;        
         
        end = clock();
        //printf("load frames: %f\n", (float)(end-start)/CLOCKS_PER_SEC);

        start = clock();
        q_frames_traverse(q_frames, rgb_arr, node_visit);
        npy_intp *dims_rgb = PyArray_SHAPE(*rgb_arr);
        if (dims_rgb[0] != queue_len(q_frames)) {
            printf("len(arr) != len(q_frames)\n");
            queue_destroy(q_frames);
            return -1;
        }            
        queue_destroy(q_frames);
        
        end = clock();
        //printf("frames traverse: %f\n", (float)(end-start)/CLOCKS_PER_SEC);
        return 0;
    }
}

static PyObject *get_num_gops(PyObject *self, PyObject *args)
{
    const char* filename;
    if (!PyArg_ParseTuple(args, "s", &filename)) 
        return NULL;

    int gop_count;
    wrap_get_num_gops(filename, &gop_count);
    return Py_BuildValue("i", gop_count);
}

static PyObject *get_gop_frame(PyObject *self, PyObject *args)
{
    const char* filename;
    int gop_target;
    if (!PyArg_ParseTuple(args, "si", &filename, &gop_target)) 
        return NULL;

    wrap_get_gop_frame(filename, gop_target);
    return Py_BuildValue("");
}

static PyObject *get_gop_frames(PyObject *self, PyObject *args)
{
    const char* filename;
    int frames;
    if (!PyArg_ParseTuple(args, "si", &filename, &frames)) 
        return NULL;
    
    int gop_count;
    wrap_get_num_gops(filename, &gop_count);

    int* gop_targets = new int[frames];
    float seg_size = float(gop_count) / frames;
    for (int i=0; i<frames; ++i) {
        gop_targets[i] = round(i*seg_size);
        gop_targets[i] = gop_targets[i] > gop_count-1 ? gop_count-1 : gop_targets[i];
        gop_targets[i] = gop_targets[i] < 0 ? 0 : gop_targets[i];
        printf("%d: %d\n", i, gop_targets[i]);
    }        
    wrap_get_gop_frames(filename, gop_targets, frames);
    if (gop_targets != NULL)
	    delete[] gop_targets; 
       
    return Py_BuildValue("");
}

static PyObject *load_gop_frames(PyObject *self, PyObject *args)
{
    const char* filename;
    int frames;
    if (!PyArg_ParseTuple(args, "si", &filename, &frames)) 
        return NULL;
    
    PyArrayObject *rgb_arr = NULL;
    
    if (decode_video(filename, frames, &rgb_arr) < 0) {
        printf("Decoding video failed.\n");
        Py_XDECREF(rgb_arr);
        return Py_None;
    } else {
        //return Py_BuildValue("O", rgb_arr);
        return (PyObject*)rgb_arr;
    }    
    
    /*
    datatype *e = (datatype *)malloc(sizeof(*e));
    queue_de(q_frames, e);
    printf("queue_de(),e=%s length=%d\n", *e, queue_len(q_frames));
    queue_traverse(q_frames, node_visit);
    queue_clear(q_frames);
    queue_traverse(q_frames, node_visit);
    //queue_destroy(q_frames);
    
   
    return Py_BuildValue("");*/
}

static PyMethodDef HWDecode_Methods[] = {
    {"get_num_gops", get_num_gops, METH_VARARGS, "Getting number of GOPs in a video"},
    {"get_gop_frame", get_gop_frame, METH_VARARGS, "Getting a gop frame"},
    {"get_gop_frames", get_gop_frames, METH_VARARGS, "Getting gop frames"},
    {"load_gop_frames", load_gop_frames, METH_VARARGS, "Loading gop frames"},
    {NULL, NULL, 0, NULL},
};

static struct PyModuleDef HWDecode_Module = {
    PyModuleDef_HEAD_INIT,
    "HWDecode",   /* name of module */
    NULL,       /* module documentation, may be NULL */
    -1,         /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    HWDecode_Methods
};

extern "C"
PyMODINIT_FUNC PyInit_HWDecode() 
{
    PyObject *m;
    m = PyModule_Create(&HWDecode_Module);
    if (m == NULL)
        return NULL;
        
    /* IMPORTANT: this must be called */
    import_array();
    
    return m;
}
