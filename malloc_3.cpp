#include <unistd.h>
#include <iostream>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>

#define MAX_ORDER 10

int firstTimeSmalloc = 1;

struct MallocMetadata {
    size_t size; ///number of bytes in this block
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
    int order;

};

size_t alignSize(size_t value, size_t alignment)
{
    size_t remainder = value % alignment;
    size_t padding;

    if(remainder == 0)
    {
        padding = 0;
    }
    else
    {
        padding = alignment - remainder;
    }

    return value + padding;
}

class MallocMetadataList{
public:
    MallocMetadata* head;
    int numOfBlocks;
    MallocMetadataList():head(nullptr),numOfBlocks(0){}
    size_t num_free_blocks();
    size_t num_free_bytes();
    size_t num_allocated_blocks();
    size_t num_allocated_bytes();
    void addNode(MallocMetadata* node);
    int removeNode(MallocMetadata* node);
    void insertToHead(MallocMetadata* node);
    size_t getNumOfFreeBytes();
    void deleteHead(size_t size);
};
size_t MallocMetadataList::getNumOfFreeBytes()
{
    size_t cnt = 0;
    MallocMetadata* current = head;

    while(current != NULL)
    {
        if (current->is_free)
            cnt += pow(2, current->order)*128 - sizeof(  MallocMetadata);
        current = current->next;
    }

    return cnt;
}

void MallocMetadataList::addNode(MallocMetadata* node){
    numOfBlocks++;
    if(!head){
        head = node;
        head->next = nullptr;
        head->prev = nullptr;
        return;
    }
    else{
        MallocMetadata* temp = head;
        MallocMetadata* lastInCurrentList = temp;
        while(temp){
            if(temp > node){
                if(temp == head){
                    head = node;
                    head->prev = nullptr;
                    node->next = temp;
                    temp->prev = node;
                } else{
                    temp->prev->next = node;
                    node->next = temp;
                    node->prev = temp->prev;
                    temp->prev = node;
                }
                return;
            }

            lastInCurrentList = temp;
            temp = temp->next;
        }
        if(lastInCurrentList){
            lastInCurrentList->next = node;
            node->next = nullptr;
        }

        node->prev = lastInCurrentList;
    }
}

int MallocMetadataList::removeNode(MallocMetadata* node){
    if(!head){
        return 0;
    }
    if(node){
        numOfBlocks--;
        node->is_free = true;
        MallocMetadata* temp = head;
        if(node == head){
            head = temp->next;
            if(temp->next){
                head->prev = nullptr;
            }
            return 0;
        }

        while(temp){
            if(temp == node){
                temp->next->prev = temp->prev;
                temp->prev->next = temp->next;
                return 1;
            }
            temp = temp->next;
        }
    }
    return 0;
}

void MallocMetadataList::insertToHead(MallocMetadata* node){

    if(node){
        if(!head){
            head = node;
            head->next = nullptr;
            head->prev = nullptr;
            numOfBlocks++;
            return;
        }
        node->next = this->head;
        node->prev = nullptr;
        head->prev = node;
        head = node;
        numOfBlocks++;
    }

}

void MallocMetadataList::deleteHead(size_t size){
    if(!head){
        return;
    }
    MallocMetadata* temp = head;
    head = temp->next;
    if(head){
        head->prev = nullptr;
    }
    temp->next = nullptr;
    temp->prev = nullptr;
    temp = nullptr;
    numOfBlocks--;
}

void* smalloc(size_t size);
void* scalloc(size_t num, size_t size);
void sfree(void* p);
void* srealloc(void* oldp, size_t size);

int OverallHeapBlocks = 32;
size_t freeBytes = 32*(128*1024 - sizeof(MallocMetadata));
size_t numOfBytes = 32*(128*1024 - sizeof(MallocMetadata));
int mmapCalls = 0;


class memoryFreeBlocks{
public:
    MallocMetadataList* array;
    size_t num_free_blocks();
    size_t num_free_bytes();
    size_t num_allocated_blocks();
    memoryFreeBlocks(){
        array = new MallocMetadataList[MAX_ORDER+1]();
        for(int i=0; i < MAX_ORDER+1 ; i++){
            MallocMetadataList* list =new  MallocMetadataList();
            array[i] = *list;
        }

        void* pb = sbrk(0);
        size_t alignedSize = alignSize((size_t)(pb), 32*128*1024);
        void* ptr = sbrk(alignedSize - (size_t)pb);
        ptr = sbrk(32*128*1024);
        if(ptr == (void*)-1){
            return;
        }
        int factor = 128*1024;
        MallocMetadata* previous = nullptr;
        for(int i=0; i < 32 ; i++){
            MallocMetadata* block = (MallocMetadata*) ((char*)ptr+factor*i);

            block->is_free = true;
            block->size = factor - sizeof(MallocMetadata);
            block->order = MAX_ORDER;
            if(i == 0){
                block->prev = nullptr;

            } else{
                if(i == 31){
                    block->next = nullptr;
                }
                previous->next = block;
                block->prev = previous;
            }

            array[MAX_ORDER].addNode(block);//changed frome inserttohead
            previous = block;
        }

    }
    void* mergeBuddies(MallocMetadata* block, size_t sizeNeeded);
    void split(MallocMetadata* node, size_t size, int order);
    MallocMetadata* getBuddy(MallocMetadata *node);
    void aux_sfree(MallocMetadata *node, int requiredOrder);
};

void* memoryFreeBlocks :: mergeBuddies(MallocMetadata* block, size_t sizeNeeded){
    int countMerges = 0;
    int currentOrder = 0;

    MallocMetadata* buddy = this->getBuddy(block);
    if(buddy < block)
        block = buddy;

    currentOrder = block->order;
    while(getBuddy(block) != NULL)
    {
        if(pow(2, ++block->order)*128 >= sizeNeeded)
        {
            //freeBytes -= pow(2, block->order)*128 - sizeof(MallocMetadata);
            countMerges++;
            break;
        }
        // freeBytes -= pow(2, block->order)*128 - sizeof(MallocMetadata);
        countMerges++;
    }


    if( getBuddy(block) == NULL)
        return NULL;

    block->order = currentOrder;


    for(int i = 0; i < countMerges; i++)
    {
        buddy = getBuddy(block);
        array[buddy->order].removeNode(buddy);
        freeBytes -= pow(2, buddy->order)*128 - sizeof(MallocMetadata);
        if(buddy->prev)
        {
            buddy->prev->next = buddy->next;
        }
        if(buddy->next)
        {
            buddy->next->prev = buddy->prev;
        }
        block->order++;
    }
    OverallHeapBlocks -= countMerges;
    numOfBytes += countMerges* sizeof(MallocMetadata);
    return block;

}

size_t memoryFreeBlocks::num_free_blocks(){
    int amount = 0;
    for(int i = 0; i < MAX_ORDER + 1; i++){
        amount += array[i].numOfBlocks;
    }
    return amount;
}

size_t memoryFreeBlocks::num_free_bytes(){
    int totalBytes = 0;
    for(int i = 0; i < MAX_ORDER + 1; i++){

        totalBytes += array[i].num_free_bytes();

    }
    return totalBytes;
}

//the global structure of memory!!!!!!!!!!!!!
memoryFreeBlocks freeBlocks = memoryFreeBlocks();

size_t MallocMetadataList:: num_free_blocks(){

    MallocMetadata* iter = head;
    int counter = 0;
    while (iter){
        if(iter->is_free){
            counter++;
        }
        iter = iter->next;
    }
    return counter;
}



size_t _num_free_blocks(){
    if(firstTimeSmalloc){
        return 0;
    }
    return freeBlocks.num_free_blocks();
}

size_t MallocMetadataList:: num_free_bytes(){
   
    size_t cnt = 0;
    MallocMetadata* current = head;

    while(current != NULL)
    {
        if(current->is_free){
            cnt += pow(2, current->order)*128 - sizeof(MallocMetadata);}
        current = current->next;
    }

    return cnt;
}

size_t _num_free_bytes(){

    if(firstTimeSmalloc){
        return 0;
    }
    return freeBytes;
    
}
size_t MallocMetadataList:: num_allocated_blocks(){

    MallocMetadata* iter = head;
    int counter = 0;
    while (iter){
        counter ++;
        iter = iter->next;
    }
    return counter;
}
size_t  _num_allocated_blocks(){
    if(firstTimeSmalloc){
        return 0;
    }
    return OverallHeapBlocks;
}
size_t MallocMetadataList:: num_allocated_bytes(){

    MallocMetadata* iter = head;
    int counter = 0;
    while (iter){
        counter+=iter->size;
        iter = iter->next;
    }
    return counter;
}
size_t _num_allocated_bytes(){
    if(firstTimeSmalloc){
        return 0;
    }
    return numOfBytes;
}

size_t _num_meta_data_bytes(){
    if(firstTimeSmalloc){
        return 0;
    }
    return OverallHeapBlocks*(sizeof(MallocMetadata));
}

size_t _size_meta_data(){
    return (sizeof (MallocMetadata));
}

int findMinOrder(int size){
    for(int i=0; i< 11 ; i++){
        if (size <= pow(2,i)*128){
            return i;
        }
    }
    return 10;
}

MallocMetadata* memoryFreeBlocks::getBuddy(MallocMetadata *node)
{
    uintptr_t intptr = reinterpret_cast<uintptr_t>(node);
    uintptr_t xorResult = intptr ^ (uintptr_t)(pow(2, node->order) * 128);
    MallocMetadata* buddy = reinterpret_cast<MallocMetadata*>(xorResult);

    return buddy;
}

void memoryFreeBlocks::split(MallocMetadata* node, size_t size, int order){
    int iterations = node->order - order;
    int oldOrder = node->order;
    for(int i = 0; i < iterations; i++){
        node->order--;
        MallocMetadata* buddy = getBuddy(node);
        if(buddy){
            int power = pow(2, node->order);
            buddy->is_free = true;
            buddy->order = node->order;
            buddy->size = power*128 - sizeof(MallocMetadata);
            node->size = power*128 - sizeof(MallocMetadata);
            array[buddy->order].addNode(buddy);
            OverallHeapBlocks++;
            numOfBytes-= sizeof(MallocMetadata);
            freeBytes-= sizeof(MallocMetadata);
        }
    }


    freeBytes+= sizeof(MallocMetadata);
    array[oldOrder].removeNode(node);//removeHead also works

}


void* smalloc(size_t size){
    firstTimeSmalloc = 0;
    
    if(_num_free_blocks() == 0 && size < 128*1024 - sizeof(MallocMetadata)){
        return nullptr;
    }
    if(size == 0){
        return nullptr;
    }
    if (size > 100000000){
        return nullptr;
    }

    void* ptr;
    //mmap
    if(size >= 128*1024 - sizeof(MallocMetadata)){
        ptr = mmap(nullptr, sizeof(MallocMetadata) + size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(ptr == (void*)-1){
            return nullptr;
        }
        if(!ptr){
            return nullptr;
        }
        
        MallocMetadata* node = (MallocMetadata*)ptr ;
        node->order = MAX_ORDER;
        node->is_free = false;
        node->size = size;
        numOfBytes += size;
        OverallHeapBlocks++;
        mmapCalls++;

        return (void*)((char*)node + (sizeof(MallocMetadata)));
    }


    int minimumOrder = findMinOrder(size);
    MallocMetadata* temp = freeBlocks.array[minimumOrder].head;
    if(temp){
        freeBlocks.array[minimumOrder].deleteHead(size);
        temp->is_free = false;
        freeBytes-= pow(2,minimumOrder)*128;
        freeBytes+= sizeof(MallocMetadata);
        temp->order = minimumOrder;
        temp->size = size;
        return (void*)((char*)temp + (sizeof(MallocMetadata)));
    }

    for(int i = minimumOrder+1; i< 11 ; i++){
        temp = freeBlocks.array[i].head;
        if(temp){
            temp->order = i;
            freeBlocks.split(temp, size, minimumOrder);
            temp->size = size;
            temp->is_free = false;

            break;
        }
    }
    if(!temp ){
        return nullptr;
    }
    freeBytes-= pow(2,minimumOrder)*128;
    return (void*)((char*)temp + (sizeof(MallocMetadata)));
//if no below 128kb block is free, do we allocate using mmap: will not be checked
}

void* scalloc(size_t num, size_t size){
    size_t total = num*size;
    if(total == 0){
        return nullptr;
    }
    if (total > 100000000){
        return nullptr;
    }
    void* ptr = smalloc(total);
    return memset(ptr,0,total);
}

void memoryFreeBlocks::aux_sfree(MallocMetadata *node, int requiredOrder){
    if(!node)
    {
        return;
    }
    if(node->size >= 128*1024 - sizeof(MallocMetadata)){
        if(node->order == MAX_ORDER){
            node->is_free = true;
            numOfBytes = numOfBytes - node->size;
            OverallHeapBlocks--;
            mmapCalls--;
            munmap((void*)((char*)node - sizeof(MallocMetadata)), sizeof(MallocMetadata) + node->size);
            return;
        }
        return;
    }
    freeBytes+= pow(2,node->order)*128 - sizeof(MallocMetadata);
    node->is_free = true;
    MallocMetadata* freeBuddy = getBuddy(node);
    if(freeBuddy){
        if(freeBuddy->is_free == false){
            array[node->order].addNode(node);
            return;
        }
    }
     if (freeBuddy && node > freeBuddy){
         MallocMetadata* temp = freeBuddy;
         freeBuddy = node;
         node = temp;
     }
    
    while(node && freeBuddy && freeBuddy->is_free && node->order < requiredOrder ){
        
        node->order++;
        node->size = pow(2,node->order)*128;
        array[freeBuddy->order].removeNode(freeBuddy);
        freeBuddy = getBuddy(node);
        OverallHeapBlocks--;
        numOfBytes+= sizeof(MallocMetadata);
        freeBytes +=  sizeof(MallocMetadata);
    }

    array[node->order].addNode(node);

}

void sfree(void* p){
    if(!p){
        return;
    }
    MallocMetadata* node = (MallocMetadata*)((char*)p - sizeof (MallocMetadata));
    if(node->is_free){
        return;
    }
    freeBlocks.aux_sfree(node, MAX_ORDER);
}


void* srealloc(void* oldp, size_t size)
{

    if(size == 0 || size > 100000000)
        return nullptr;

    if(oldp ==nullptr)
        return (smalloc(size));

    MallocMetadata* oldpMD = (MallocMetadata*)((char*)oldp - sizeof(  MallocMetadata));


    if(oldpMD->order >= findMinOrder(size))
    {
        oldpMD->size = size;
        return oldp;
    }

    if(oldpMD->size + sizeof(MallocMetadata) > 128*1024) //mmap
    {
        void* newBlock = smalloc(size);
        MallocMetadata* newAllocated = (MallocMetadata*)((char*)newBlock - sizeof(MallocMetadata));
        memcpy(newAllocated, oldp, oldpMD->size);
        sfree(oldp);
        return newBlock;
    }

    MallocMetadata* buddy = freeBlocks.getBuddy(oldpMD);

    void* retBlock = freeBlocks.mergeBuddies(oldpMD, size);
    if(!buddy || !retBlock) //no enough buddies found
    {
        sfree(oldp);
        void* newBlockk = smalloc(size);
        memcpy(newBlockk, oldp, oldpMD->size);
        return newBlockk;
    }

    memcpy((char*)retBlock+sizeof(MallocMetadata), oldp, oldpMD->size);
    return (char*)retBlock+sizeof(MallocMetadata);
}

