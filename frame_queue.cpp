#include "frame_queue.h"

void visit(link_queue q)
{
    /* 头结点 */
    link_node p = q -> front -> next;
    if(!p)
    {
        printf("队列为空");
    }
    while(p)
    {
        printf("%d ", p -> data);
        p = p -> next;
    }
    printf("\n");
}


void wrap_queue(link_queue q)
{
    queue_en(q, 1);
    queue_en(q, 2);
    printf("length=%d\n", queue_len(q));
    queue_en(q, 3);
    printf("length=%d\n", queue_len(q));
    queue_en(q, 4);
    printf("length=%d\n", queue_len(q));
}

int main()
{
    link_queue q = queue_init();

    wrap_queue(q);
    printf("length=%d\n", queue_len(q));
    
    queue_en(q, 5);
    printf("length=%d\n", queue_len(q));
    queue_en(q, 6);
    printf("length=%d\n", queue_len(q));
    queue_traverse(q,visit);
    datatype *e = (datatype *)malloc(sizeof(*e));
    queue_de(q,e);
    printf("queue_de(),e=%d length=%d\n", *e, queue_len(q));
    queue_traverse(q, visit);
    queue_clear(q);
    queue_traverse(q, visit);
    printf("length:%d\n", queue_len(q));
}
