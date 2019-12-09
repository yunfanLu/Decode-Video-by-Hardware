#ifndef FRAME_QUEUE_H_
#define FRAME_QUEUE_H_

#include<stdio.h>
#include<stdlib.h>

extern "C" {
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavutil/pixdesc.h>
    #include <libavutil/hwcontext.h>
    #include <libavutil/opt.h>
    #include <libavutil/avassert.h>
    #include <libavutil/imgutils.h>
    #include <libswscale/swscale.h>
}

/* 队列的数据类型 */
typedef struct datatype{
    int width;
    int height;
    int wrap;
    uint8_t * data;
} datatype;

/* 静态链的数据结构 */
typedef struct q_node{
    datatype data;
    struct q_node *next;
}q_node, *link_node;

typedef struct l_queue{
    /* 队头指针 */
    q_node *front;
    /* 队尾指针 */
    q_node *rear;
} *link_queue;
/* 静态链的初始化 */
link_queue queue_init();
/* 判断队列是否为空,若为空
 * 返回1
 * 否则返回0
*/
int queue_empty(link_queue q);

/* 插入元素e为队q的队尾新元素 
 * 插入成功返回1
 * 队满返回0
*/
int queue_en(link_queue q, datatype e);
/* 队头元素出队
 * 用e返回出队元素,并返回1
 * 若队空返回0
*/
int queue_de(link_queue q, datatype *e);
/* 清空队 */
void queue_clear(link_queue q);
/* 销毁队 */
void queue_destroy(link_queue q);
/* 获得队头元素
 * 队列非空,用e返回队头元素,并返回1
 * 否则返回0
*/
int get_front(link_queue q, datatype *e );
/* 获得队长 */
int queue_len(link_queue q);
/* 遍历队 */
void queue_traverse(link_queue q, void(*visit)(link_queue q));
void visit(link_queue q);


link_queue queue_init()
{
    /* 新建头结点 */
    link_node new_node = (link_node)malloc(sizeof(q_node));
    new_node -> next = NULL;
    /* 指针结点 */
    link_queue q = (link_queue)malloc(sizeof(*q));
    q -> front = q -> rear = new_node;
    return q;
}


int queue_empty(link_queue q)
{
    return q -> front == q -> rear;
}


int queue_en(link_queue q, datatype e)
{
    /* 新建数据结点 */
    link_node new_node = (link_node)malloc(sizeof(q_node));
    /* 内存分配失败 */
    if(!new_node)
        return 0;
    new_node -> data = e;
    new_node -> next = NULL;
    q -> rear -> next = new_node;
    q -> rear = new_node;
    return 1;
}

int queue_de(link_queue q, datatype *e)
{
    /* 队列为空 */
    if (q -> front == q -> rear)
        return 0;

    *e = q -> front -> next -> data;
    link_node temp = q -> front -> next;
    q -> front -> next = temp -> next;
    /* 防止丢失尾指针 */
    if (temp == q->rear->next)
        q -> rear = q-> front; 
    free(temp);
    temp = NULL;
    return 1;
}

void queue_clear(link_queue q)
{
    /* 头结点 */
    link_node head = q -> front -> next;
    head -> next = NULL;
    q -> front = q -> rear = head;
    /* 第一个结点 */
    link_node temp = head -> next;
    while(temp)
    {
        link_node p = temp;
        temp = p -> next;
        free(p);
        p = NULL;
    }
}


void queue_destroy(link_queue q)
{
    queue_clear(q);
    free(q);
    q = NULL;
}


int get_front(link_queue q, datatype *e)
{
    /* 队为空 */
    if (q -> front == q -> rear)
        return 0;

    *e = q -> front -> next -> data;
    link_node temp = q -> front -> next;
    q -> front -> next = temp -> next;
    free(temp);
    temp = NULL;
    return 1;
}


int queue_len(link_queue q)
{
    /* 头结点 */
    link_node p = q -> front -> next;
    /* 计数器 */
    int count = 0;
    while(p)
    {
        count += 1;
        p = p -> next;
    }
    return count;
}


void queue_traverse(link_queue q, void(*visit)(link_queue q))
{
    visit(q);
}

#endif // FRAME_QUEUE_
