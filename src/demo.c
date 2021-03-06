#include "network.h"
#include "detection_layer.h"
#include "region_layer.h"
#include "cost_layer.h"
#include "utils.h"
#include "parser.h"
#include "box.h"
#include "image.h"
#include "demo.h"
#include <sys/time.h>

#define DEMO 1

#ifdef OPENCV         //也就是说只有编译了opencv的话，demo才是可以用的，下面这些代码才开始编译

/**
 * 静态变量当然都是存储在静态存储区的，只能当前文件使用，其他文件即使使用extern也是无法使用的
 **/

static char **demo_names;         //char **型的变量，这些在demo函数里用到，作为静态变量
static image **demo_alphabet;
static int demo_classes;

static network *net;
static image buff [3];
static image buff_letter[3];     //buff_letter就是image类型的
static int buff_index = 0;
static void * cap;
static float fps = 0;
static float demo_thresh = 0;
static float demo_hier = .5;
static int running = 0;

static int tracking=0;           //tracking的标志位，满足跟踪条件的标志位
static c_rect track_box;            //detection转track_box是需要的track_box,这是自己定义的一个数据类型，box是float的不太适合在这里用
static int track_begin=0;        //是否是跟踪开始的标志，如果是第一帧的话，在跟踪框架中需要初始化track。否则就不需要
       //跟踪结果，返回值
static int xx,yy,ww,hh;
static float psr=0;
static float track_fps=0;


static int demo_frame = 3;
static int demo_index = 0;
static float **predictions;
static float *avg;
static int demo_done = 0;
static int demo_total = 0;
double demo_time;






detection *get_network_boxes(network *net, int w, int h, float thresh, float hier, int *map, int relative, int *num);

int size_network(network *net)
{
    int i;
    int count = 0;
    for(i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            count += l.outputs;
        }
    }
    return count;
}

void remember_network(network *net)
{
    int i;
    int count = 0;
    for(i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            memcpy(predictions[demo_index] + count, net->layers[i].output, sizeof(float) * l.outputs);
            count += l.outputs;
        }
    }
}

detection *avg_predictions(network *net, int *nboxes)
{
    int i, j;
    int count = 0;
    fill_cpu(demo_total, 0, avg, 1);
    for(j = 0; j < demo_frame; ++j){
        axpy_cpu(demo_total, 1./demo_frame, predictions[j], 1, avg, 1);
    }
    for(i = 0; i < net->n; ++i){
        layer l = net->layers[i];
        if(l.type == YOLO || l.type == REGION || l.type == DETECTION){
            memcpy(l.output, avg + count, sizeof(float) * l.outputs);
            count += l.outputs;
        }
    }
    detection *dets = get_network_boxes(net, buff[0].w, buff[0].h, demo_thresh, demo_hier, 0, 1, nboxes);
    return dets;
}

//  这个就是预测的函数，只是以多线程的方式放入demo的主函数了，我一开始看代码的时候把多线程相关的
//  全部忽略掉了，后来实在是找不到，原来在这里，这样就好了。线程中的检测函数
void *detect_in_thread(void *ptr)
{

    if(tracking==0)
    {
    running = 1;       //就是一个标志位，表示正在进行当前的进程吧？
    float nms = .4;     //nms是最大值检测的

    layer l = net->layers[net->n-1];
    float *X = buff_letter[(buff_index+2)%3].data;   //数据，这就是直接图像数据的指针了
    network_predict(net, X);    //网络预测

    /*
       if(l.type == DETECTION){
       get_detection_boxes(l, 1, 1, demo_thresh, probs, boxes, 0);
       } else */
    remember_network(net);      
    detection *dets = 0;
    int nboxes = 0;
    dets = avg_predictions(net, &nboxes);    
    //获得检测的结果



    //注释掉的这一部分应该是以前的版本
    /*
       int i,j;
       box zero = {0};
       int classes = l.classes;
       for(i = 0; i < demo_detections; ++i){
       avg[i].objectness = 0;
       avg[i].bbox = zero;
       memset(avg[i].prob, 0, classes*sizeof(float));
       for(j = 0; j < demo_frame; ++j){
       axpy_cpu(classes, 1./demo_frame, dets[j][i].prob, 1, avg[i].prob, 1);
       avg[i].objectness += dets[j][i].objectness * 1./demo_frame;
       avg[i].bbox.x += dets[j][i].bbox.x * 1./demo_frame;
       avg[i].bbox.y += dets[j][i].bbox.y * 1./demo_frame;
       avg[i].bbox.w += dets[j][i].bbox.w * 1./demo_frame;
       avg[i].bbox.h += dets[j][i].bbox.h * 1./demo_frame;
       }
    //copy_cpu(classes, dets[0][i].prob, 1, avg[i].prob, 1);
    //avg[i].objectness = dets[0][i].objectness;
    }
     */

    //非极大值抑制，
    if (nms > 0) do_nms_obj(dets, nboxes, l.classes, nms);
    
    //打印一些参数
    //帧率和识别的目标
    printf("\033[2J");
    printf("\033[1;1H");
    printf("\nFPS:%.1f\n",fps);
    printf("Objects:\n\n");
    image display = buff[(buff_index+2) % 3];    //需要显示的当前的图片

    //图上面画框之类的函数，但是这里实际实际上并没有显示，显示的话还是需要用opencv来做
    draw_detections(display, dets, nboxes, demo_thresh, demo_names, demo_alphabet, demo_classes);

    box bbox=dets[0].bbox;
    track_box.x=bbox.x*display.w;
    track_box.y=bbox.y*display.h;
    track_box.w=bbox.w*display.w;
    track_box.h=bbox.h*display.h;

    /*
    printf("from (x,y)=(%d,%d)\t",xx,yy);
    printf("(w,h)=(%d,%d)\n",ww,hh);
    printf("%d\n",nboxes);
    */
    /*这里一定要来判断bboxes是否是大于0的，如果不是的话，
    那么dets[0]和dets[0].prob[0]这两个指针都是空的，把空指针传入printf就只能等着程序崩溃了。
    陷阱就是虽然指针是野的，但是是有值的，可以进行计算，我一开始写的是：
    float prob=dets[0].prob[0]*100;
    printf("%f\n",prob);
    第一句计算是不会报错的，第二句直接崩溃。
    */
    if(nboxes>0)
    {

        printf("the prob is:\t%f\n",dets[0].prob[0]*100);
        if(dets[0].prob[0]*100>=95)   //如果检测到的置信率大于95%的话就转入跟踪
        {
            tracking=1;        //进入跟踪状态
            track_begin=1;     //这是第一次进入跟踪状态
        }
    }
    //释放内存应该是
    free_detections(dets, nboxes);
    

    demo_index = (demo_index + 1)%demo_frame;
    running = 0;
    return 0;
    }
    //这里不用else，也就是说我是需要上面的运行结束之后，当前的帧就要转换到tracking模式下来做第一帧
    //图像的显示函数是不能自己来写的，因为这个东西全是交给线程来做的，所以这里只能修改不能做显示，要不会卡死的。
    if(tracking==1)      
    {
        //printf("this is tracking process!!!\n");
        image current_img = buff[(buff_index+2) % 3];
        //printf("track_begin%d\t\n",track_begin);    //这里看着是1啊，但是到后面就直接成了172了，神奇
        //printf("bbox:%d\t%d\t%d\t%d\n",track_box.x,track_box.y,track_box.w,track_box.h); 
        c_rect tracking_res;  
        //demo_time=what_time_is_it_now();
        kcf(current_img,track_box,&xx,&yy,&ww,&hh,&psr,&track_fps); 
        //fps = 1./(what_time_is_it_now() - demo_time);      //这个就是帧率了
        printf("FPS:\t%f\n",1.0/track_fps);
        //这里输出跟踪结果，在这里来看的话基本都是可以的，下面就是要画框了，啊啊啊，终于可以有点进度了（190228）
        printf("tracking res:\t %d   %d    %d    %d     PSR:   %f\n",xx,yy,ww,hh,psr);
        
        draw_box(current_img,xx,yy,xx+ww,yy+hh,0.0,0.0,255);
    }
}

void *fetch_in_thread(void *ptr)    //线程函数
{
    free_image(buff[buff_index]);     //释放buff
    buff[buff_index] = get_image_from_stream(cap);     //重新从视频中读入照片
    if(buff[buff_index].data == 0) {   //如果没有读取到图片，则视频流结束
        demo_done = 1;
        return 0;
    }
    letterbox_image_into(buff[buff_index], net->w, net->h, buff_letter[buff_index]);  
    //如果读取成功的话就还是要letterbox，这个函数看demo主函数里的注释
    return 0;
}

void *display_in_thread(void *ptr)      //显示图像，可以调整阈值参数,对应QRST四个字母，具体需要看下面的
{
    int c = show_image(buff[(buff_index + 1)%3], "Demo", 1);      //这个就是显示图像了
    if (c != -1) c = c%256;
    if (c == 27) {
        demo_done = 1;
        return 0;
    } else if (c == 82) {           //R   
        demo_thresh += .02;
    } else if (c == 84) {           //T
        demo_thresh -= .02;
        if(demo_thresh <= .02) demo_thresh = .02;
    } else if (c == 83) {           //  S
        demo_hier += .02;
    } else if (c == 81) {          // Q
        demo_hier -= .02;
        if(demo_hier <= .0) demo_hier = .0;
    }
    return 0;
}

void *display_loop(void *ptr)
{
    while(1){
        display_in_thread(0);
    }
}

void *detect_loop(void *ptr)
{
    while(1){
        detect_in_thread(0);
    }
}

//这就是函数的定义了，我好像也没有找到函数的声明，demo.h里面好像也没有声明,这个就是定义加声明了吧
/**
 *  cfgfile：    网络结构配置文件路径
 *  weightfile： 权重文件
 *  thresh：     阈值，如果给的话就会只显示阈值之上的，默认的阈值好像是0.5？
 *  cam_index:   这个就是摄像头的索引了
 * 
 * 
 **/

void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, int classes, int delay, char *prefix, int avg_frames, float hier, int w, int h, int frames, int fullscreen)
{
    //demo_frame = avg_frames;
    image **alphabet = load_alphabet();     
    //函数原型在image.c里面，是为了加载"data/labels/%d_%d.png"下的字符集的，应该是为了后面画框的时候加文字方便吧
    demo_names = names;          //char**
    demo_alphabet = alphabet;    //image **
    demo_classes = classes;      //int     
    demo_thresh = thresh;        //float   阈值，高于阈值的才显示
    demo_hier = hier;            //float 
    printf("Demo\n");      //打印一个参数
    net = load_network(cfgfile, weightfile, 0);   //导入网路结构
    set_batch_network(net, 1);        

    // typedef unsigned long int pthread_t;
    // 这里应该是为了多线程处理的，没有看懂
    // 所以这里应该就是创建了两个线程，
    pthread_t detect_thread; 
    pthread_t fetch_thread;
   

    //srand()需要配合rand()使用，这里就是产生伪随机数据种子。
    srand(2222222);    

    int i;
    demo_total = size_network(net);
    predictions = calloc(demo_frame, sizeof(float*));
    for (i = 0; i < demo_frame; ++i){
        predictions[i] = calloc(demo_total, sizeof(float));
    }
    avg = calloc(demo_total, sizeof(float));     //开辟空间

    //如果有视频文件，则打开视频文件，如果没有，则打开摄像头
    if(filename){
        printf("video file: %s\n", filename);
        cap = open_video_stream(filename, 0, 0, 0, 0);
    }else{
        cap = open_video_stream(0, cam_index, w, h, frames);
    }

    if(!cap) error("Couldn't connect to webcam.\n");
    //如果是没有视频或者摄像头，那么报错

    //这里是把图片复制了三个？
    buff[0] = get_image_from_stream(cap);
    buff[1] = copy_image(buff[0]);
    buff[2] = copy_image(buff[0]);
    buff_letter[0] = letterbox_image(buff[0], net->w, net->h);
    buff_letter[1] = letterbox_image(buff[0], net->w, net->h);
    buff_letter[2] = letterbox_image(buff[0], net->w, net->h);
    /**
     * buff_letter 是一个image类型的数组
     * 这里相当于是把图像缩放到和网络输入一样的大小喽，所以这里才是刚刚把图片读入进来
     * letterbox_image()   函数原型在  image.c里
     * 输入图像以及一个宽和高，反正是把图像调整到尺寸是w,h,
     * 还要保证原图的宽高比，如果不够的话就填充灰度
    **/
    int count = 0;    //计数索引
    if(!prefix){
        make_window("Demotest", 1352, 1013, fullscreen);     //创建一个窗口，原函数在IMAGE_OPENCV.CPP里
    }

    demo_time = what_time_is_it_now();      //当前时间
 
    //demo_done 应该是视频读取完成的一个标志，默认是0，在文件的最开始有定义
    while(!demo_done){
        buff_index = (buff_index + 1) %3;    //字面意思应该是缓存索引
        if(pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
        if(pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");
        if(!prefix){
            fps = 1./(what_time_is_it_now() - demo_time);   //这个就是帧率了
            demo_time = what_time_is_it_now();        //现在的时间
            display_in_thread(0);         //这就是个显示图像的，检测是用多线程组织的
        }else{
            char name[256];
            sprintf(name, "%s_%08d", prefix, count);
            save_image(buff[(buff_index + 1)%3], name);   //重命名图片保存
        }
        pthread_join(fetch_thread, 0); 
        pthread_join(detect_thread, 0);    //所以加入线程就是这个命令了
        ++count;
    }
}

/*
   void demo_compare(char *cfg1, char *weight1, char *cfg2, char *weight2, float thresh, int cam_index, const char *filename, char **names, int classes, int delay, char *prefix, int avg_frames, float hier, int w, int h, int frames, int fullscreen)
   {
   demo_frame = avg_frames;
   predictions = calloc(demo_frame, sizeof(float*));
   image **alphabet = load_alphabet();
   demo_names = names;
   demo_alphabet = alphabet;
   demo_classes = classes;
   demo_thresh = thresh;
   demo_hier = hier;
   printf("Demo\n");
   net = load_network(cfg1, weight1, 0);
   set_batch_network(net, 1);
   pthread_t detect_thread;
   pthread_t fetch_thread;

   srand(2222222);

   if(filename){
   printf("video file: %s\n", filename);
   cap = cvCaptureFromFile(filename);
   }else{
   cap = cvCaptureFromCAM(cam_index);

   if(w){
   cvSetCaptureProperty(cap, CV_CAP_PROP_FRAME_WIDTH, w);
   }
   if(h){
   cvSetCaptureProperty(cap, CV_CAP_PROP_FRAME_HEIGHT, h);
   }
   if(frames){
   cvSetCaptureProperty(cap, CV_CAP_PROP_FPS, frames);
   }
   }

   if(!cap) error("Couldn't connect to webcam.\n");

   layer l = net->layers[net->n-1];
   demo_detections = l.n*l.w*l.h;
   int j;

   avg = (float *) calloc(l.outputs, sizeof(float));
   for(j = 0; j < demo_frame; ++j) predictions[j] = (float *) calloc(l.outputs, sizeof(float));

   boxes = (box *)calloc(l.w*l.h*l.n, sizeof(box));
   probs = (float **)calloc(l.w*l.h*l.n, sizeof(float *));
   for(j = 0; j < l.w*l.h*l.n; ++j) probs[j] = (float *)calloc(l.classes+1, sizeof(float));

   buff[0] = get_image_from_stream(cap);
   buff[1] = copy_image(buff[0]);
   buff[2] = copy_image(buff[0]);
   buff_letter[0] = letterbox_image(buff[0], net->w, net->h);
   buff_letter[1] = letterbox_image(buff[0], net->w, net->h);
   buff_letter[2] = letterbox_image(buff[0], net->w, net->h);
   ipl = cvCreateImage(cvSize(buff[0].w,buff[0].h), IPL_DEPTH_8U, buff[0].c);

   int count = 0;
   if(!prefix){
   cvNamedWindow("Demo", CV_WINDOW_NORMAL); 
   if(fullscreen){
   cvSetWindowProperty("Demo", CV_WND_PROP_FULLSCREEN, CV_WINDOW_FULLSCREEN);
   } else {
   cvMoveWindow("Demo", 0, 0);
   cvResizeWindow("Demo", 1352, 1013);
   }
   }

   demo_time = what_time_is_it_now();

   while(!demo_done){
buff_index = (buff_index + 1) %3;
if(pthread_create(&fetch_thread, 0, fetch_in_thread, 0)) error("Thread creation failed");
if(pthread_create(&detect_thread, 0, detect_in_thread, 0)) error("Thread creation failed");
if(!prefix){
    fps = 1./(what_time_is_it_now() - demo_time);
    demo_time = what_time_is_it_now();
    display_in_thread(0);
}else{
    char name[256];
    sprintf(name, "%s_%08d", prefix, count);
    save_image(buff[(buff_index + 1)%3], name);
}
pthread_join(fetch_thread, 0);
pthread_join(detect_thread, 0);
++count;
}
}
*/
#else
void demo(char *cfgfile, char *weightfile, float thresh, int cam_index, const char *filename, char **names, int classes, int delay, char *prefix, int avg, float hier, int w, int h, int frames, int fullscreen)
{
    fprintf(stderr, "Demo needs OpenCV for webcam images.\n");
}
#endif

