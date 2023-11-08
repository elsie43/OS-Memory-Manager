#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#define DISK_SIZE (2048 * 20)
typedef struct
{
    int processID;
    int index;
    int pfn;
    long time;
    int reference;
    int present;
} page;

typedef struct QueueNode
{
    page thePage;
    struct QueueNode *next;
} QueueNode;

typedef struct Queue
{
    QueueNode *head;
    QueueNode *tail;
} Queue;
FILE *fp_out;
// Global variable
long TLB[32][3]; // 0: vpn; 1: pfn; 2: last access time
page **PT;       // page table
int **PhyMem;    // 0: process('?'-'A'); 1: vpn
int UsedMax = -1;
long count[20][2] = {0}; // 0: reference count; 1:page fault count
Queue *LocalUsed[20];    // for local allocation
Queue *GlobalUsed;
Queue *FreeFrame;
// Queue *Disk;
int Disk[DISK_SIZE][3];
QueueNode *victim_local[20];
QueueNode *victim_global;
char *TLB_policy;
char *page_policy;
char *frame_policy;
int numofProcess = 0;
int numofPage = 0;
int numofFrame = 0;

// function declare
void get_sys_info();
int isSubstring(char *str, char *substr);
void QueueInit(Queue *q);
void QueueDestroy(Queue *q);
void QueuePush(Queue *q, page x);
page QueuePop(Queue *q);
page QueueFront(Queue *q);
page QueueBack(Queue *q);
int inQueue(Queue *q, int _id, int _vpn);
page QueueGetPage(Queue *q, int _id, int _vpn);
int QueueEmpty(Queue *q);
int QueueSize(Queue *q);
QueueNode *QueueFindVictim(Queue *q, QueueNode *lastPoint);
void delNode(Queue *q, int _id, int _vpn);
void initFree(int number);
int searchTLB(int Ref);
void rmpageTLB(int Pfn);
void TLB_replace(int whichProcess, int _vpn, int _pfn, long time);
void TLB_miss_handler(int whichProcess, int _vpn, int _pfn, long time);
int allocateFreeFrame(int whichProcess, int _vpn, int _pfn, long time);

void initFree(int number)
{
    FreeFrame = (Queue *)malloc(sizeof(Queue));
    QueueInit(FreeFrame);
    for (int i = 0; i < number; i++)
    {
        page pushPage;
        pushPage.pfn = i;
        QueuePush(FreeFrame, pushPage);
    }
}

int searchDisk(int whichProcess, int _vpn)
{
    int i = 0;
    for (; i <= UsedMax; i++)
    {
        if (Disk[i][0] == whichProcess && Disk[i][1] == _vpn)
            return i;
    }
    return -1;
}
void pageOut(int whichProcess, int _vpn, int _pfn)
{
    int i = 0;
    for (; i <= UsedMax; i++)
    {
        if (Disk[i][0] == -1)
        {
            fprintf(fp_out, "to %d", i);
            printf("to %d", i);
            Disk[i][0] = whichProcess;
            Disk[i][1] = _vpn;
            Disk[i][2] = _pfn;
            return;
        }
    }
    UsedMax++;
    fprintf(fp_out, "to %d", i);
    printf("to %d", i);
    Disk[i][0] = whichProcess;
    Disk[i][1] = _vpn;
    Disk[i][2] = _pfn;
}
int searchTLB(int Ref)
{
    for (int i = 0; i < 32; i++)
    {
        if (TLB[i][0] == Ref) // TLB reach
            return TLB[i][1];
    }
    return -1; // TLB miss
}
void rmpageTLB(int Pfn)
{
    for (int i = 0; i < 32; i++)
    {
        if (TLB[i][1] == Pfn)
        {
            TLB[i][0] = -1;
            TLB[i][1] = -1;
            TLB[i][2] = -1;
            return;
        }
    }
}

int updateTLB(int whichProcess, int _vpn, int _pfn, long time)
{
    int i = 0;
    for (; i < 32; i++)
    {
        if (TLB[i][0] == -1 || TLB[i][0] == _vpn)
        {
            TLB[i][0] = _vpn;
            TLB[i][1] = _pfn;
            TLB[i][2] = time;
            return i;
        }
    }
    return i;
}

void flushTLB()
{
    // memset(TLB, 0, sizeof TLB);
    for (int i = 0; i < 32; i++)
    {
        TLB[i][0] = -1;
        TLB[i][1] = -1;
        TLB[i][2] = -1;
    }
}

void page_fault_handler(int whichProcess, int _vpn, int _pfn, long time)
{
    count[whichProcess][1]++;     // page fault ++
    if (QueueSize(FreeFrame) > 0) // page fault but has free frame
    {
        _pfn = allocateFreeFrame(whichProcess, _vpn, _pfn, time);
        fprintf(fp_out, "Process %c, TLB Miss, Page Fault, %d, ", whichProcess + 'A', _pfn);
        fprintf(fp_out, "Evict -1 of Process %c to -1, %d<<-1\n", whichProcess + 'A', _vpn);
        printf("Process %c, TLB Miss, Page Fault, %d, ", whichProcess + 'A', _pfn);
        printf("Evict -1 of Process %c to -1, %d<<-1\n", whichProcess + 'A', _vpn);
        // printf("check pfn: %d\n", _pfn);
        if (_pfn != -1)
        {
            fprintf(fp_out, "Process %c, TLB Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);
            printf("Process %c, TLB Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);
        }
    }
    else
    { // no free frame -> replace
        page delPage;
        fprintf(fp_out, "Process %c, TLB Miss, Page Fault, ", whichProcess + 'A');
        printf("Process %c, TLB Miss, Page Fault, ", whichProcess + 'A');
        // if in disk -> page in
        int src = searchDisk(whichProcess, _vpn);
        if (src != -1) // page fault but in disk
        {              // search evict
            int delProcess = PhyMem[Disk[src][2]][0];
            int delVpn = PhyMem[Disk[src][2]][1];
            fprintf(fp_out, "%d, Evict %d of Process %c ", Disk[src][2], delVpn, delProcess + 'A');
            printf("%d, Evict %d of Process %c ", Disk[src][2], delVpn, delProcess + 'A');

            PT[delProcess][delVpn].pfn = (-'K');
            PT[delProcess][delVpn].present = 0;

            page page_page_in;
            page_page_in.processID = Disk[src][0];
            page_page_in.index = Disk[src][1];
            page_page_in.pfn = Disk[src][2];
            page_page_in.time = time;
            page_page_in.reference = 1;
            page_page_in.present = 1;

            pageOut(delProcess, delVpn, page_page_in.pfn);
            Disk[src][0] = -1;
            Disk[src][1] = -1;
            Disk[src][2] = -1;

            fprintf(fp_out, ", %d<<%d\n", _vpn, src);
            printf(", %d<<%d\n", _vpn, src);
            // printf("debug here!!!\n");

            if (whichProcess != page_page_in.processID || _vpn != page_page_in.index)
                printf("Error from page_fault_handler()\n");
            PT[whichProcess][_vpn] = page_page_in;

            // update TLB
            int search_pfn = searchTLB(page_page_in.index);
            if (search_pfn == -1) // TLB miss
            {
                TLB_replace(page_page_in.processID, page_page_in.index, page_page_in.pfn, page_page_in.time);
            }

            fprintf(fp_out, "Process %c, TLB Hit, %d=>%d\n", page_page_in.processID + 'A', page_page_in.index, page_page_in.pfn);
            printf("Process %c, TLB Hit, %d=>%d\n", page_page_in.processID + 'A', page_page_in.index, page_page_in.pfn);

            // printf("process: %d, vpn: %d\n", whichProcess, _vpn);

            PhyMem[page_page_in.pfn][0] = whichProcess;
            PhyMem[page_page_in.pfn][1] = _vpn;

            return;
        }
        else // not in disk
        {

            if (strcmp(page_policy, "CLOCK") == 0) // CLOCK
            {
                if (strcmp(frame_policy, "GLOBAL") == 0)
                {
                    victim_global = QueueFindVictim(GlobalUsed, victim_global);
                    delPage = victim_global->thePage;
                    printf("%s", "clock here 2\n");
                }
                else // local
                {
                    victim_local[whichProcess] = QueueFindVictim(LocalUsed[whichProcess], victim_local[whichProcess]);
                    delPage = victim_local[whichProcess]->thePage;
                }
            }
            else // FIFO
            {
                if (strcmp(frame_policy, "GLOBAL") == 0)
                {
                    delPage = QueuePop(GlobalUsed);
                    // printf("after pop global size: %d\n", QueueSize(GlobalUsed));
                }
                else // local
                    delPage = QueuePop(LocalUsed[whichProcess]);
            }
            rmpageTLB(delPage.pfn);
            // printf("%d, Evict %d of Process %c ", Disk[src][2], delVpn, delProcess + 'A');
            fprintf(fp_out, "%d, Evict %d of Process %c ", delPage.pfn, delPage.index, delPage.processID + 'A');
            printf("%d, Evict %d of Process %c ", delPage.pfn, delPage.index, delPage.processID + 'A');
            pageOut(delPage.processID, delPage.index, delPage.pfn);

            // QueuePush(Disk, delPage); // put into disk
            PT[PhyMem[delPage.pfn][0]][delPage.index].pfn = (0 - 'K');
            PT[PhyMem[delPage.pfn][0]][delPage.index].present = 0;
            // printf("delete page pf number: %d\n", delPage.pfn);
            if (strcmp(page_policy, "CLOCK") == 0)
            {
                if (strcmp(frame_policy, "GLOBAL") == 0)
                {
                    printf("clock here 3\n");
                    victim_global = victim_global->next;
                    delNode(GlobalUsed, delPage.processID, delPage.index);
                    printf("delete page pf number: %d\n", delPage.pfn);
                }
                else
                {
                    victim_local[whichProcess] = victim_local[whichProcess]->next;
                    delNode(GlobalUsed, delPage.processID, delPage.index);
                }
            }
            QueuePush(FreeFrame, delPage); // release frame
            _pfn = allocateFreeFrame(whichProcess, _vpn, _pfn, time);
            fprintf(fp_out, ", %d<<-1\n", _vpn);
            printf(", %d<<-1\n", _vpn);
            if (_pfn != -1)
            {
                fprintf(fp_out, "Process %c, TLB Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);
                printf("Process %c, TLB Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);
            }
        }
    }
}

int allocateFreeFrame(int whichProcess, int _vpn, int _pfn, long time)
{
    // printf("free frame left: %d\n", QueueSize(FreeFrame));
    page getPage = QueuePop(FreeFrame); // allocate free frame
    // printf("Get Page from free: %d\n", getPage.pfn);
    PhyMem[getPage.pfn][0] = whichProcess; // mark as occupied
    PhyMem[getPage.pfn][1] = _vpn;
    getPage.processID = whichProcess;
    getPage.index = _vpn;
    getPage.reference = 1;
    getPage.present = 1;
    getPage.time = time;
    PT[whichProcess][_vpn] = getPage;
    // printf("update 1\n");
    if (updateTLB(whichProcess, _vpn, getPage.pfn, time) == 32) // TLB is full: kick
    {
        TLB_replace(whichProcess, _vpn, getPage.pfn, time);
    }
    if (strcmp(frame_policy, "LOCAL") == 0)
    { // local replacement
        QueuePush(LocalUsed[whichProcess], getPage);
    }
    else if (strcmp(frame_policy, "GLOBAL") == 0)
    {
        // printf("before push, page ref = %d\n", getPage.reference);
        QueuePush(GlobalUsed, getPage);
        // printf("global size: %d\n", QueueSize(GlobalUsed));
    }
    else
        printf("impossible 1\n");

    return getPage.pfn;
}

void TLB_miss_handler(int whichProcess, int _vpn, int _pfn, long time)
{ // in PT? yes -> get value and update TLB; no -> pagee fault
    // printf("%d\n", PT[whichProcess][_vpn].pfn);
    // if (_pfn == -1 || _pfn == -'K') // page fault occurs
    if (PT[whichProcess][_vpn].pfn == -1 || PT[whichProcess][_vpn].pfn == -'K') // page fault occurs
    {
        page_fault_handler(whichProcess, _vpn, _pfn, time); // pfn wrong
    }
    else
    { // TLB miss but page hit -> print info and update TLB
        _pfn = PT[whichProcess][_vpn].pfn;
        fprintf(fp_out, "Process %c, TLB Miss, Page Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);
        printf("Process %c, TLB Miss, Page Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);
        // printf("update 2\n");
        if (updateTLB(whichProcess, _vpn, _pfn, time) == 32) // TLB is full: replace
        {
            // printf("TLB replace 2\n");
            TLB_replace(whichProcess, _vpn, _pfn, time);
        }

        fprintf(fp_out, "Process %c, TLB Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);
        printf("Process %c, TLB Hit, %d=>%d\n", whichProcess + 'A', _vpn, _pfn);

        PT[whichProcess][_vpn].time = time;
        PT[whichProcess][_vpn].reference = 1;
    }
}
void TLB_replace(int whichProcess, int _vpn, int _pfn, long time)
{
    int record_index = 0;
    if (strcmp(TLB_policy, "RANDOM") == 0)
    {
        record_index = rand() % 32; // random select for replacing
    }
    else if (strcmp(TLB_policy, "LRU") == 0)
    {
        long tempTime = time; // set the first page's time as base time
        for (int j = 0; j < 32; j++)
        {
            if (TLB[j][2] < tempTime && TLB[j][2] != -1)
            {
                tempTime = TLB[j][2];
                record_index = j;
            }
        }
    }
    else
        printf("TLB_replace() error\n");
    TLB[record_index][0] = -1;
    TLB[record_index][1] = -1;
    TLB[record_index][2] = -1;
    // printf("update 3 with-> process: %d, vpn: %d, pfn: %d\n", whichProcess, _vpn, _pfn);
    updateTLB(whichProcess, _vpn, _pfn, time); // update again
}
int main()
{
    srand(time(NULL));
    get_sys_info();

    fp_out = fopen("my_out.txt", "w+");
    char ch;
    char numstr[10];

    PT = malloc(20 * sizeof *PT);
    for (int i = 0; i < 20; i++)
    {
        PT[i] = malloc(numofPage * sizeof *PT[i]);
    }

    PhyMem = malloc(numofFrame * sizeof *PhyMem);
    for (int i = 0; i < numofFrame; i++)
    {
        PhyMem[i] = malloc(2 * sizeof *PhyMem[i]);
    }

    GlobalUsed = (Queue *)malloc(sizeof(Queue));
    victim_global = (QueueNode *)malloc(sizeof(QueueNode));
    QueueInit(GlobalUsed);

    for (int i = 0; i < 20; i++)
    {
        // init LocalUsed
        LocalUsed[i] = (Queue *)malloc(sizeof(Queue));
        QueueInit(LocalUsed[i]);
        victim_local[i] = (QueueNode *)malloc(sizeof(QueueNode));
        for (int j = 0; j < numofPage; j++) // init page table
        {
            PT[i][j].pfn = -1;
            PT[i][j].time = -1;
            PT[i][j].reference = 0;
            PT[i][j].present = 0;
        }
    }
    for (int i = 0; i < DISK_SIZE; i++)
    {
        Disk[i][0] = -1;
        Disk[i][1] = -1;
        Disk[i][2] = -1;
    }
    initFree(numofFrame); // init Free Frame List

    FILE *fp = fopen("trace.txt", "r");
    char currentRef = 0;
    char preRef = 0;
    long timeCount = 0;
    while ((ch = fgetc(fp)) != EOF)
    {
        if (ch == '(')
        {
            ch = fgetc(fp);
            currentRef = ch;
            if (preRef != currentRef) // switch process
                flushTLB();
            preRef = currentRef;
            while (ch < '0' || ch > '9')
                ch = fgetc(fp);
            int j = 0;
            while ('0' <= ch && ch <= '9')
            {
                numstr[j] = ch;
                j++;
                ch = fgetc(fp);
            }
            numstr[j] = '\0';
            printf("\n%c, %s\n", currentRef, numstr);

            // start working here for every round
            timeCount++;
            int processNo = currentRef - 'A';
            count[processNo][0]++;
            int vpnRef = atoi(numstr); // str to int
            int pfn = searchTLB(vpnRef);
            if (pfn == -1) // TLB miss
            {
                // printf("TLB miss from main -> process: %d; vpn: %d ; pfn: %d\n", processNo, vpnRef, pfn);
                TLB_miss_handler(processNo, vpnRef, pfn, timeCount);
            }
            else
            { // TLB hit
                fprintf(fp_out, "Process %c, TLB Hit, %d=>%d\n", currentRef, vpnRef, pfn);
                printf("TLB hit from main: Process %c, TLB Hit, %d=>%d\n", currentRef, vpnRef, pfn);
                updateTLB(processNo, vpnRef, pfn, timeCount);
                PT[processNo][vpnRef].time = timeCount;
                PT[processNo][vpnRef].reference = 1;
            }
        }
    }
    fclose(fp);
    fclose(fp_out);
    return 0;
}

int isSubstring(char *str, char *substr)
{
    char *result = strstr(str, substr);
    int position = result - str;
    if (position > 0)
        return position;
    else
        return 0;
}

QueueNode *QueueFindVictim(Queue *q, QueueNode *lastPoint)
{
    QueueNode *current = lastPoint;
    // printf("current point ref: %d\n", current->thePage.reference);
    if (q == NULL)
    {
        printf("queue doesn't exist\n");
    }

    while (1)
    {

        if (current == NULL)
        {
            printf("current is NULL\n");
            current = q->head;
        }
        else
        {
            if (current->thePage.reference == 0) // clock point to 0 -> can kick
            {
                printf("clock 0 \n");
                return current;
            }

            else if (current->thePage.reference == 1)
            {
                printf("clock 1 \n");
                current->thePage.reference = 0;
                current = current->next;
            }
            // else
            //  printf("ref bit : %d\n", current->thePage.reference);
        }
    }
}
void QueueInit(Queue *q)
{
    if (q == NULL)
    {
        printf("Queue malloc error!\n");
        exit(-1);
    }
    q->head = NULL;
    q->tail = NULL;
}

void QueueDestroy(Queue *q)
{
    QueueNode *current = q->head;
    while (current)
    {
        QueueNode *_next = current->next;
        free(current);
        current = _next;
    }
    q->head = q->tail = NULL;
}

void QueuePush(Queue *q, page x)
{
    QueueNode *newnode = (QueueNode *)malloc(sizeof(QueueNode));
    if (q == NULL)
    {
        printf("queue doesn't exist\n");
    }
    if (newnode == NULL)
    {
        printf("malloc error!\n");
        exit(-1);
    }
    newnode->thePage = x;
    newnode->next = NULL;
    if (q->head == NULL)
    {
        q->head = q->tail = newnode;
        return;
    }
    else
    {
        q->tail->next = newnode;
        q->tail = newnode;
    }
    // printf("Push success\n");
}

page QueuePop(Queue *q)
{
    page catchPage = q->head->thePage;
    QueueNode *next = q->head->next;
    free(q->head);
    q->head = next;
    if (q->head == NULL)
    {
        q->tail = NULL;
    }
    return catchPage;
}
void delNode(Queue *q, int _id, int _vpn)
{

    // Store head node

    QueueNode *temp = q->head;
    QueueNode *prev = NULL;
    // If head node itself holds
    // the key to be deleted
    if (temp != NULL && (temp->thePage).processID == _id && (temp->thePage).index == _vpn)
    {
        // printf("pfn: %d, dpage: %d\n", (temp->thePage).pfn, dpage.pfn);
        q->head = temp->next; // Changed head
        // delete temp;            // free old head
        // printf("delNode() e1\n");
        return;
        // return temp->thePage;
    }
    // Else Search for the key to be deleted,
    // keep track of the previous node as we
    // need to change 'prev->next' */
    else
    {

        while (temp != NULL && !((temp->thePage).processID == _id && (temp->thePage).index == _vpn))
        {
            prev = temp;
            temp = temp->next;
        }
        // If key was not present in linked list
        // if (temp == NULL)
        // return NULL;

        if (temp->next == NULL)
            q->tail = temp;

        // printf("dpage: %d\n", dpage.pfn);
        //  Unlink the node from linked list
        prev->next = temp->next;

        // printf("in delnode()\n");

        // Free memory
        // printf("delNode() e2\n");
        // return temp->thePage;
    }
}
page QueueFront(Queue *q)
{
    return q->head->thePage;
}

page QueueBack(Queue *q)
{
    return q->tail->thePage;
}

int QueueEmpty(Queue *q)
{
    if (q == NULL)
        printf("Queue NULL\n");
    return q->head == NULL ? 1 : 0;
}

int QueueSize(Queue *q)
{
    QueueNode *current = q->head;
    int size = 0;
    while (current)
    {
        ++size;
        current = current->next;
    }
    return size;
}

int inQueue(Queue *q, int _id, int _vpn)
{
    QueueNode *current = q->head;
    while (current)
    {
        if ((current->thePage).processID == _id && (current->thePage).index == _vpn)
        {
            return 1;
        }
        current = current->next;
    }
    return 0;
}

page QueueGetPage(Queue *q, int _id, int _vpn)
{
    // must check inQueue() first
    QueueNode *current = q->head;
    while (current)
    {
        if ((current->thePage).processID == _id && (current->thePage).index == _vpn)
        {
            return current->thePage;
        }
        current = current->next;
    }
    printf("GET PAGE FAILED\n");
    return (q->head)->thePage; // just in case
}

void get_sys_info()
{
    FILE *fp = fopen("sys_my.txt", "r");
    char ch;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    if (fp == NULL)
        exit(EXIT_FAILURE);

    for (int i = 0; i < 3; i++)
    {
        if ((read = getline(&line, &len, fp)) != -1)
        {
            if (isSubstring(line, "LRU"))
                TLB_policy = "LRU";
            else if (isSubstring(line, "RANDOM"))
                TLB_policy = "RANDOM";
            else if (isSubstring(line, "CLOCK"))
                page_policy = "CLOCK";
            else if (isSubstring(line, "FIFO"))
                page_policy = "FIFO";
            else if (isSubstring(line, "GLOBAL"))
                frame_policy = "GLOBAL";
            else if (isSubstring(line, "LOCAL"))
                frame_policy = "LOCAL";

            // printf("%d\n\n", position);
        }
    }

    printf("\n");
    printf("TLB_policy: %s\n", TLB_policy);
    printf("page_policy: %s\n", page_policy);
    printf("frame_policy: %s\n\n", frame_policy);
    char numstr[10];

    while ((ch = fgetc(fp)) != EOF)
    {
        if (ch == ':')
        {
            // while ((ch = fgetc(fp)) == ' ')
            ch = fgetc(fp); // get space
            int j = 0;
            ch = fgetc(fp); // get num
            // printf("this c: %c\n", ch);
            while ('0' <= ch && ch <= '9')
            {
                numstr[j] = ch;
                j++;
                ch = fgetc(fp);
            }
            numstr[j] = '\0';
            // printf("%s\n", numstr);
            if (!numofProcess)
                numofProcess = atoi(numstr);
            else if (!numofPage)
                numofPage = atoi(numstr);
            else
                numofFrame = atoi(numstr);
        }
        // printf("%c", ch);
    }
    printf("process: %d\n", numofProcess);
    printf("numofPage: %d\n", numofPage);
    printf("frame: %d\n", numofFrame);
    fclose(fp);
    memset(numstr, 0, sizeof numstr);
    if (line)
        free(line);
}
