#ifndef __GDT_H__
#define __GDT_H__

# include <linux/types.h>

class GlobalDescriptorTable
{
    public:
    class SegmentDescriptor //段
    {
        private:
        uint16_t limit_lo;
        uint16_t base_lo;
        uint8_t base_hi;
        uint8_t type;
        uint8_t flags_limit_hi;
        uint8_t base_vhi;

        public:
        SegmentDescriptor(uint32_t base, uint32_t limit, uint8_t type);  //base
        uint32_t Base();
        uint32_t Limit();
    } __attribute__ ((packed)); // 取消字节对齐

    SegmentDescriptor nullSegmentSelector;  //空
    SegmentDescriptor unusedSegmentSelector;
    SegmentDescriptor codeSegmentSelector; //代码段
    SegmentDescriptor dataSegmentSelector; // 数据段

    public:
    GlobalDescriptorTable();
    ~GlobalDescriptorTable();

    uint16_t CodeSegmentSelector(); //偏移
    uint16_t DataSegmentSelector();
};

#endif