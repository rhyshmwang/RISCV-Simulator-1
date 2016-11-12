#include <iostream>
#include <elf.h>
#include <fstream>
#include <vector>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/time.h>
#include <map>
#include <string>
#include <algorithm>
using namespace std;

bool verbose=false;
int totalcnt=0;
const char * RegName [32]={"zero","ra","sp","gp","tp","t0","t1","t2","s0","s1","a0","a1","a2","a3","a4","a5","a6","a7","s2","s3","s4","s5","s6","s7","s8","s9","s10","s11","t3","t4","t5","t6"};
const char * fRegName [32]={"ft0","ft1","ft2","ft3","ft4","ft5","ft6","ft7","fs0","fs1","fa0","fa1","fa2","fa3","fa4","fa5","fa6","fa7","fs2","fs3","fs4","fs5","fs6","fs7","fs8","fs9","fs10","fs11","ft8","ft9","ft10","ft11",};
    
void ecall();
uint64_t PC, PC_next;
char ErrorMSG[256];
map<string, int> instr_counter;

struct MemoryBlock 
{
    char * Content;
    uint64_t StartAddr;
    uint64_t EndAddr;
    MemoryBlock(char * Content_, uint64_t StartAddr_, uint64_t EndAddr_)
    {
        Content=Content_;
        StartAddr=StartAddr_;
        EndAddr=EndAddr_;
    }
};


void Error(const char * msg)
{
    printf("%lx:\t%s", PC, msg);
    exit(1);
}



class VirtualMemory
{
    vector<MemoryBlock*>blocks;
    public:
    char * getPaddr(uint64_t Vaddr)
    {
        int l=blocks.size();
        for(int i=0; i<l; i++)
        {
            if(Vaddr>=blocks[i]->StartAddr && Vaddr<blocks[i]->EndAddr)
            {
                return blocks[i]->Content + (Vaddr-blocks[i]->StartAddr);
            }
        }
        sprintf(ErrorMSG,"Memory Error: %lx\n",Vaddr);
        Error(ErrorMSG);
    }
    
    
    void load(char * Content,uint64_t MemSize,uint64_t StartAddr)
    {
        blocks.push_back(new MemoryBlock(Content,StartAddr,StartAddr+MemSize));
    }
    
    uint8_t ReadByte(uint64_t Vaddr)
    {
        
        return *(uint8_t *)getPaddr(Vaddr);
    }
    
    void WriteByte(uint64_t Vaddr, uint8_t Byte)
    {   
        *(uint8_t *)getPaddr(Vaddr)=Byte;
    }
    
    uint16_t ReadHalfword(uint64_t Vaddr)
    {
        return *(uint16_t *)getPaddr(Vaddr);
    }
    
    void WriteHalfword(uint64_t Vaddr,uint16_t Halfword)
    {  
       *(uint16_t *)getPaddr(Vaddr)=Halfword;
    }
    
    uint32_t ReadWord(uint64_t Vaddr)
    {
        return *(uint32_t *)getPaddr(Vaddr);
    }
    
    void WriteWord(uint64_t Vaddr,uint32_t Word)
    {
        *(uint32_t *)getPaddr(Vaddr)=Word;
    }
    
    uint64_t ReadDoubleword(uint64_t Vaddr)
    {
        return *(uint64_t *)getPaddr(Vaddr);
    }
    
    void WriteDoubleword(uint64_t Vaddr,uint64_t Doubleword)
    {
        *(uint64_t *)getPaddr(Vaddr)=Doubleword;
    }
};

class RegisterFile {
    uint64_t reg[32];
    public:
    uint64_t & operator [] (int i)
    {
        if(i==0)
        {
            reg[0]=0;
        }
        return reg[i];
    }
};
union Reg{
    float f;
    double d;
    uint64_t l;
    Reg(float f_)
    {
        l=0;
        f=f_;
    }
    Reg(double d_)
    {
        d=d_;
    }
    Reg(uint64_t l_)
    {
        l=l_;
    }
};

class instruction {
    public :
    uint32_t code;
    
    instruction(uint32_t code_) {code=code_;}
    uint32_t opcode() {return code & 0b1111111;}
    uint32_t rd() {return (code & 0b111110000000)>>7;}
    uint32_t func3() {return (code & 0b111000000000000)>>12;}
    uint32_t rs1() {return (code & 0b11111000000000000000)>>15;}
    uint32_t rs2() {return (code & 0b1111100000000000000000000)>>20;}
    uint32_t rs3() {return (code >> 27);}
    uint32_t fmt() {return ( (code >> 25)&0b11 );}
    uint32_t func7() {return (code & 0b11111110000000000000000000000000)>>25;}
    uint32_t shamt() {return rs2();}
    uint64_t imm_I(bool SignExt=true) {if(SignExt)return ((int64_t)((int)code)>>20);else return code>>20;}
    uint64_t imm_S(bool SignExt=true) {if(SignExt)return ((int64_t)(int)((((int)code)>>20)&(~0b11111)|rd()));
                                            return (code>>20)&(~0b11111)|rd();}
    uint64_t imm_SB(bool SignExt=true)
    {
        if(SignExt)
            return (int64_t)(int)((( (int)code>>20) &(~0b11111)|(rd()&(~1))) &(~ (0b100000000000))) | ((code&0b10000000)<<4);
        return ((( code>>20) &(~0b11111)|(rd()&(~1))) &(~ (0b100000000000))) | ((code&0b10000000)<<4);
    }

    uint64_t imm_U(bool SignExt=true) {if(SignExt)return int64_t(int(code &(~0b111111111111))); return code &(~0b111111111111);}
    uint64_t imm_UJ(bool SignExt=true)
    {
        if(SignExt)
            return (int64_t)(int)((((int)code)>>20&(~1)) & (~0b11111111100000000000) | (((code>>20)&1)<<11) | (code & 0b11111111000000000000));
        return (code>>20&(~1)) & (~0b11111111100000000000) | (((code>>20)&1)<<11) | (code & 0b11111111000000000000);
    }
    
    uint32_t func6() {return (code & 0b11111100000000000000000000000000)>>26;}
    uint32_t shamt6() {return (code & 0b11111100000000000000000000)>>20;}

};


RegisterFile x,f;
VirtualMemory mem;

uint64_t & sp=x[2];
uint64_t & gp=x[3];
uint64_t & a0=x[10];
uint64_t & a1=x[11];
uint64_t & a2=x[12];
uint64_t & a3=x[13];
uint64_t & a4=x[14];
uint64_t & a5=x[15];
uint64_t & a6=x[16];
uint64_t & a7=x[17];

uint64_t & s3=x[19];
uint64_t & s10=x[26];

void counter(string str)
{
	map<string, int>::iterator it = instr_counter.find(str);
	if(it != instr_counter.end())
   		++instr_counter[str];
   	else
   		instr_counter[str] = 1;
}

bool RV32M (instruction instr)
{
    if(instr.opcode()==0b0110011&&instr.func7()==0b0000001)
    {
        switch (instr.func3()) {
            case 0b000://MUL
                if(verbose) printf("mul\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
                x[instr.rd()] = x[instr.rs1()] * x[instr.rs2()];
                counter("mul");
                return true;
                
            case 0b101://DIVU
                if(verbose) printf("divu\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
                x[instr.rd()] = x[instr.rs1()] / x[instr.rs2()];
                counter("divu");
                return true;
                
            case 0b111://REMU
                if(verbose) printf("remu\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
                x[instr.rd()] = x[instr.rs1()] % x[instr.rs2()];
                counter("remu");
                return true;
                
            default: return false;
        }
    }
    return false;
}

bool RV64M (instruction instr)
{
    if(instr.opcode()==0b0111011 && instr.func7()==0b0000001)
    {
        switch (instr.func3()) {
            case 0b000: //MULW
                if(verbose) printf("mulw\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
                x[instr.rd()] = int64_t(int32_t(x[instr.rs1()]) * int32_t(x[instr.rs2()]));
                counter("mulw");
                return true;
        
            case 0b100: //DIVW
                if(verbose) printf("divw\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
                x[instr.rd()] = int64_t(int32_t(x[instr.rs1()]) / int32_t(x[instr.rs2()]));
                counter("divw");
				return true;
                
            case 0b101: //DIVWU
                if(verbose) printf("divwu\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
                x[instr.rd()] = int64_t(int32_t(uint32_t(x[instr.rs1()]) / uint32_t(x[instr.rs2()])));
                counter("divwu");
				return true;
                
            default: return false;
        }
    }
    return false;
}

int main(int argc, char ** argv)
{
    
    ifstream fin (argv[1], ios::binary);
    Elf64_Ehdr Elfhdr;
    fin.read((char *)(&Elfhdr), sizeof(Elf64_Ehdr));
    Elf64_Phdr Prohdr;
    fin.read((char *)(&Prohdr), sizeof(Elf64_Phdr));
    uint64_t segsize=0x2000000+Prohdr.p_memsz;
    char * Content=new char [segsize];
    memset(Content, 0, segsize);
    fin.seekg(Prohdr.p_offset);
    fin.read(Content, Prohdr.p_filesz);
    mem.load(Content, segsize, Prohdr.p_vaddr);
    
    sp=0xfefffb50;
    mem.load(new char [0x2000000], 0x2000000, 0xfe000000);
    PC=Elfhdr.e_entry;
    
    while(true)
    {
        totalcnt++;
        instruction instr=mem.ReadWord(PC);
        PC_next=PC+4;
        if(verbose)printf("%x:\t%08x\t\t", (uint32_t)PC, instr.code);
        switch(instr.opcode())
        {
            
            case 0b0010111: //AUIPC
                if(verbose) printf("auipc\t%s,0x%lx", RegName[instr.rd()], instr.imm_U(true));
                x[instr.rd()] = instr.imm_U() + PC;
        		counter("auipc");
                break;
			
			case 0b0110111: //LUI
			    if(verbose) printf("lui\t%s,0x%lx", RegName[instr.rd()], instr.imm_U(true));
                    x[instr.rd()] = instr.imm_U();
                counter("lui");
				break;
                
            case 0b1101111: //JAL
                if(verbose) printf("jal\t%s,0x%lx", RegName[instr.rd()], instr.imm_UJ(true));
                x[instr.rd()] = PC + 4;
                PC_next=instr.imm_UJ()+PC;
                counter("jal");
                break;
                
            case 0b1100111: //JALR
                if(instr.func3()==0b000)
                {
                    if(verbose) printf("jalr\t%s,%s,0x%lx", RegName[instr.rd()],RegName[instr.rs1()], instr.imm_I(true));
                    x[instr.rd()] = PC + 4;
                    PC_next=instr.imm_I()+x[instr.rs1()];
                }
                else Error("Invalid instruction\n");
                counter("jalr");
                break;
			case 0b0000011: 
				switch(instr.func3())
				{
					case 0b000: //LB
						if(verbose) printf("lb\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(true));
						x[instr.rd()] = int64_t(int8_t(mem.ReadByte(instr.imm_I() + x[instr.rs1()])));
						counter("lb");
						break;
						
					case 0b001: //LH
						if(verbose) printf("lh\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(true));
						x[instr.rd()] = int64_t(int16_t(mem.ReadHalfword(instr.imm_I() + x[instr.rs1()])));
						counter("lh");
						break;
						
					case 0b010: //LW
						if(verbose) printf("lw\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(true));
                        x[instr.rd()] = int64_t(int32_t(mem.ReadWord(instr.imm_I() + x[instr.rs1()])));
						counter("lw");
						break;
					
					case 0b011: //LD
						if(verbose) printf("ld\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(true));
						x[instr.rd()] = mem.ReadDoubleword(instr.imm_I() + x[instr.rs1()]);
						counter("ld");
						break;
					
					case 0b100: //LBU
						if(verbose) printf("lbu\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(true));
						x[instr.rd()] = uint64_t(uint8_t(mem.ReadByte(instr.imm_I() + x[instr.rs1()])));
						counter("lbu");
						break;
							
					case 0b101: //LHU
						if(verbose) printf("lhu\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(true));
						x[instr.rd()] = uint64_t(uint16_t(mem.ReadHalfword(instr.imm_I() + x[instr.rs1()])));
						counter("lhu");
						break;	
					
					case 0b110: //LWU
						if(verbose) printf("lwu\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(true));
						x[instr.rd()] = uint64_t(uint32_t(mem.ReadWord(instr.imm_I() + x[instr.rs1()])));
						counter("lwu");
						break;
                        
                    default: Error("Invalid instruction\n");
				}
                
                break;
                
			case 0b0100011:
				switch(instr.func3())
				{
					case 0b000: //SB
						if(verbose) printf("sb\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_S());
						mem.WriteByte(x[instr.rs1()] + instr.imm_S(), x[instr.rs2()]);
						counter("sb");
						break;
						
					case 0b001: //SH
						if(verbose) printf("sh\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_S());
						mem.WriteHalfword(x[instr.rs1()] + instr.imm_S(), x[instr.rs2()]);
						counter("sh");
						break;
						
					case 0b010: //SW
						if(verbose) printf("sw\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_S());
						mem.WriteWord(x[instr.rs1()] + instr.imm_S(), x[instr.rs2()]);
						counter("sw");
						break;
						
					case 0b011: //SD
						if(verbose) printf("sd\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_S());
						mem.WriteDoubleword(x[instr.rs1()] + instr.imm_S(), x[instr.rs2()]);
						counter("sd");
						break;
                    
                    default: Error("Invalid instruction\n");
				}
                break;
                
			case 0b0110011:
                if(RV32M(instr))
                    break;
				switch(instr.func3())
				{
					case 0b000: 
						switch(instr.func7())
						{
							case 0b0000000: //ADD
								if(verbose) printf("add\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = x[instr.rs1()] + x[instr.rs2()];
								counter("add");
								break;
								
							case 0b0100000: //SUB
								if(verbose) printf("sub\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = x[instr.rs1()] - x[instr.rs2()];
								counter("sub");
								break;
                            default: Error("Invalid instruction\n");
						}
                        break;
					case 0b001: //SLL
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("sll\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
						x[instr.rd()] = x[instr.rs1()] << x[instr.rs2()];
						counter("sll");
						break;
					
					case 0b010: //SLT
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("sll\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
						if((int64_t)x[instr.rs1()] < (int64_t)x[instr.rs2()]) x[instr.rd()] = 1;
						else x[instr.rd()] = 0;
						counter("slt");
						break;
					
					case 0b011: //SLTU
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("sll\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
						if(x[instr.rs1()] < x[instr.rs2()]) x[instr.rd()] = 1;
						else x[instr.rd()] = 0;
						counter("sltu");
						break;
						
					case 0b100: //XOR
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("xor\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
						x[instr.rd()] = x[instr.rs1()] ^ x[instr.rs2()];
						counter("xor");
						break;
						
					case 0b101:
						switch(instr.func7())
						{
							case 0b0000000: //SRL
								if(verbose) printf("srl\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = x[instr.rs1()] >> x[instr.rs2()];
								counter("srl");
								break;
								
							case 0b0100000: //SRA
								if(verbose) printf("sra\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = (int64_t)x[instr.rs1()] >> x[instr.rs2()];
								counter("sra");
								break;
                                
                            default: Error("Invalid instruction\n");
						}
                        break;
						
					case 0b110: //OR
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("or\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
						x[instr.rd()] = x[instr.rs1()] | x[instr.rs2()];
						counter("or");
						break;
					
					case 0b111: //AND
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("and\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
						x[instr.rd()] = x[instr.rs1()] & x[instr.rs2()];
						counter("and");
						break;
                    
                    default: Error("Invalid instruction\n");
				}
                break;
			
			case 0b1110011: //SCALL
                if(instr.code!=instr.opcode())Error("Invalid instruction\n");
                if(verbose)printf("ecall");
                ecall();
                counter("scall");
				break;
				
			case 0b1100011:
				switch(instr.func3())
				{
					case 0b000: //BEQ
						if(verbose) printf("beq\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_SB());
						if(x[instr.rs1()] == x[instr.rs2()]) PC_next = PC + instr.imm_SB();
						counter("beq");
						break;
						
					case 0b001: //BNE
						if(verbose) printf("bne\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_SB());
						if(x[instr.rs1()] != x[instr.rs2()]) PC_next = PC + instr.imm_SB();
						counter("bne");
						break;
						
					case 0b100: //BLT
						if(verbose) printf("blt\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_SB());
						if((int64_t)x[instr.rs1()] < (int64_t)x[instr.rs2()]) PC_next= PC + instr.imm_SB();
						counter("blt");
						break;
						
					case 0b101: //BGE
						if(verbose) printf("bge\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_SB());
						if((int64_t)x[instr.rs1()] >= (int64_t)x[instr.rs2()]) PC_next = PC + instr.imm_SB();
						counter("bge");
						break;
						
					case 0b110: //BLTU
						if(verbose) printf("bltu\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_SB());
						if(x[instr.rs1()] < x[instr.rs2()]) PC_next = PC + instr.imm_SB();
						counter("bltu");
						break;
						
					case 0b111: //BGEU
						if(verbose) printf("bgeu\t%s,%s,%ld", RegName[instr.rs1()], RegName[instr.rs2()], (int64_t)instr.imm_SB());
						if(x[instr.rs1()] >= x[instr.rs2()]) PC_next = PC + instr.imm_SB();
						counter("bgeu");
						break;
                        
                    default: Error("Invalid instruction\n");
				}
                break;
				
			case 0b0010011:
				switch(instr.func3())
				{
					case 0b000: //ADDI
						if(verbose) printf("addi\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I());
						x[instr.rd()] = x[instr.rs1()] + instr.imm_I();
						counter("addi");
						break;
						
					
					case 0b010: //SLTI
						if(verbose) printf("slti\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I());
						if((int64_t)x[instr.rs1()] < (int64_t)instr.imm_I()) x[instr.rd()] = 1;
						else x[instr.rd()] = 0;
						counter("slti");
						break;
						
					case 0b011: //SLTIU
						if(verbose) printf("sltiu\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I());
						if(x[instr.rs1()] < instr.imm_I())
                        {
                            //cout<<"1";
                            x[instr.rd()] = 1;
                        }
						else x[instr.rd()] = 0;
						counter("sltiu");
						break;
						
					case 0b100: //XORI
						if(verbose) printf("xori\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(false));
						x[instr.rd()] = x[instr.rs1()] ^ instr.imm_I(false);
						counter("xori");
						break;
                    
                    case 0b001: //SLLI
                        if(instr.func6()!=0b000000)Error("Invalid instruction\n");
                        if(verbose) printf("slli\t%s,%s,%d", RegName[instr.rd()], RegName[instr.rs1()], instr.shamt6());
                        x[instr.rd()] = x[instr.rs1()] << instr.shamt6();
                        counter("slli");
						break;
                        
					case 0b101:
						switch(instr.func6())
						{
							case 0b000000: //SRLI
								if(verbose) printf("srli\t%s,%s,%d", RegName[instr.rd()], RegName[instr.rs1()], instr.shamt6());
								x[instr.rd()] = x[instr.rs1()] >> instr.shamt6();
								counter("srli");
								break;
								
							case 0b010000: //SRAI
								if(verbose) printf("srai\t%s,%s,%d", RegName[instr.rd()], RegName[instr.rs1()], instr.shamt6());
								x[instr.rd()] = (int64_t)x[instr.rs1()] >> instr.shamt6();
								counter("srai");
								break;
                                
                            default: Error("Invalid instruction\n");
						}
						break;
						
					case 0b110: //ORI
						if(verbose) printf("ori\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(false));
						x[instr.rd()] = x[instr.rs1()] | instr.imm_I(false);
						counter("ori");
						break;
						
					case 0b111: //ANDI
						if(verbose) printf("andi\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], (int64_t)instr.imm_I(false));
						x[instr.rd()] = x[instr.rs1()] & instr.imm_I(false);
						counter("andi");
						break;
                        
                    default: Error("Invalid instruction\n");
				}
				break;
				
			case 0b0011011:
				switch(instr.func3())
				{
					case 0b000: //ADDIW
                        
						if(verbose) printf("addiw\t%s,%s,%ld", RegName[instr.rd()], RegName[instr.rs1()], instr.imm_I());
						x[instr.rd()] = (int64_t)((int)x[instr.rs1()] + (int)instr.imm_I());
						counter("addiw");
						break;
						
					case 0b001: //SLLIW
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("slliw\t%s,%s,%d", RegName[instr.rd()], RegName[instr.rs1()], instr.shamt());
						x[instr.rd()] = (int64_t)((int)x[instr.rs1()] << instr.shamt());
						counter("slliw");
						break;
						
					case 0b101: //SRLIW
						switch(instr.func7())
						{
							case 0b0000000: //SRLIW
								if(verbose) printf("srliw\t%s,%s,%d", RegName[instr.rd()], RegName[instr.rs1()], instr.shamt());
								x[instr.rd()] = (int64_t)(int)((unsigned int)x[instr.rs1()] >> instr.shamt());
								counter("srliw");
								break;
								
							case 0b0100000: //SRAIW
								if(verbose) printf("sraiw\t%s,%s,%d", RegName[instr.rd()], RegName[instr.rs1()], instr.shamt());
								x[instr.rd()] = (int64_t)((int)x[instr.rs1()] >> instr.shamt());
								counter("sraiw");
								break;
                                
                            default: Error("Invalid instruction\n");
						}
						break;
                        
                    default: Error("Invalid instruction\n");
				}
				break;
				
			case 0b0111011:
                if(RV64M(instr))
                    break;
				switch(instr.func3())
				{
					case 0b000:
						switch(instr.func7())
						{
							case 0b0000000: //ADDW
								if(verbose) printf("addw\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = (int64_t)((int)x[instr.rs1()] + (int)x[instr.rs2()]);
								counter("addw");
								break;
								
							case 0b0100000: //SUBW
								if(verbose) printf("subw\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = (int64_t)((int)x[instr.rs1()] - (int)x[instr.rs2()]);
								counter("subw");
								break;
                                
                            default: Error("Invalid instruction\n");
						}
						break;
					
					case 0b001: //SLLW
                        if(instr.func7()!=0b0000000)Error("Invalid instruction\n");
						if(verbose) printf("sllw\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
						x[instr.rd()] = (int64_t)(int)((unsigned int)x[instr.rs1()] << (int)x[instr.rs2()]);
						counter("sllw");
						break;
						
					case 0b101:
						switch(instr.func7())
						{
							case 0b0000000: //SRLW
								if(verbose) printf("srlw\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = (int64_t)(int)((unsigned int)x[instr.rs1()] >> (int)x[instr.rs2()]);
								counter("srlw");
								break;
								
							case 0b0100000: //SRAW
								if(verbose) printf("sraw\t%s,%s,%s", RegName[instr.rd()], RegName[instr.rs1()], RegName[instr.rs2()]);
								x[instr.rd()] = (int64_t)((int)x[instr.rs1()] >> (int)x[instr.rs2()]);
								counter("sraw");
								break;
                                
                            default: Error("Invalid instruction\n");
						}
						break;
                        
                    default: Error("Invalid instruction\n");
				}
				break;
            
            
                
                
            case 0b0100111:
                switch (instr.func3()) {
                    case 0b011://fsd
                        if(verbose) printf("fsd\t%s,%s,%ld", RegName[instr.rs1()], fRegName[instr.rs2()], instr.imm_S());
                        mem.WriteDoubleword(x[instr.rs1()] + instr.imm_S(), f[instr.rs2()]);
                        counter("fsd");
						break;
                        
                    case 0b010://fsw
                        if(verbose) printf("fsd\t%s,%s,%ld", RegName[instr.rs1()], fRegName[instr.rs2()], instr.imm_S());
                        mem.WriteWord(x[instr.rs1()] + instr.imm_S(), f[instr.rs2()]);
                        counter("fsw");
						break;
                        
                    default: Error("Invalid instruction\n");
                        
                }
                break;
                
                
            
            case 0b0000111:
                switch (instr.func3()) {
                    case 0b011://fld
                        if(verbose) printf("fld\t%s,%s,%ld", fRegName[instr.rd()], RegName[instr.rs1()], instr.imm_I());
                        f[instr.rd()] = mem.ReadDoubleword(instr.imm_I() + x[instr.rs1()]);
                        counter("fld");
						break;
                       
                    case 0b010://flw
                        if(verbose) printf("flw\t%s,%s,%ld", fRegName[instr.rd()], RegName[instr.rs1()], instr.imm_I());
                        f[instr.rd()] = mem.ReadWord(instr.imm_I() + x[instr.rs1()]);
                        counter("flw");
						break;

                    default: Error("Invalid instruction\n");
                        
                }
                break;
            
            case 0b1000011:
                switch (instr.fmt()) {
                    case 0b01://FMADD.D
                        if(verbose) printf("fmadd.d\t%s,%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()], fRegName[instr.rs3()]);
                        f[instr.rd()] = Reg (Reg(f[instr.rs1()]).d * Reg(f[instr.rs2()]).d + Reg(f[instr.rs3()]).d).l;
                        counter("fmadd.d");
						break;
                        
                    default: Error("Invalid instruction\n");
                        
                }
                break;
                
            case 0b1010011:
                switch (instr.func7()) {
                    case 0b0001001://FMUL.D
                        if(verbose) printf("fmul.d\t%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                        f[instr.rd()] = Reg (Reg(f[instr.rs1()]).d * Reg(f[instr.rs2()]).d).l;
                        counter("fmul.d");
						break;
                        
                    case 0b0001000://FMUL.F
                        if(verbose) printf("fmul.f\t%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                        f[instr.rd()] = Reg (Reg(f[instr.rs1()]).f * Reg(f[instr.rs2()]).f).l;
                        counter("fmul.f");
						break;
                        
                    case 0b0001101://FDIV.D
                        if(verbose) printf("fdiv.d\t%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                        f[instr.rd()] = Reg (Reg(f[instr.rs1()]).d / Reg(f[instr.rs2()]).d).l;
                        counter("fdiv.d");
						break;
                        
                    case 0b0001100://FDIV.S
                        if(verbose) printf("fdiv.s\t%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                        f[instr.rd()] = Reg (Reg(f[instr.rs1()]).f / Reg(f[instr.rs2()]).f).l;
                        counter("fdiv.s");
						break;
                        
                    case 0b0000101://FSUB.D
                        if(verbose) printf("fsub.d\t%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                        f[instr.rd()] = Reg (Reg(f[instr.rs1()]).d - Reg(f[instr.rs2()]).d).l;
                        counter("fsub.d");
						break;
                    
                    case 0b0000001://FADD.D
                        if(verbose) printf("fadd.d\t%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                        f[instr.rd()] = Reg (Reg(f[instr.rs1()]).d + Reg(f[instr.rs2()]).d).l;
                        counter("fadd.d");
						break;
                        
                    case 0b1110001://FMV.X.D
                        if(instr.rs2()!=0||instr.func3()!=0)Error("Invalid instruction\n");
                        if(verbose) printf("fmv.x.d\t%s,%s", RegName[instr.rd()], fRegName[instr.rs1()]);
                        x[instr.rd()] = f[instr.rs1()];
                        counter("fmv.x.d");
						break;
                        
                    case 0b1111001://FMV.D.X
                        if(instr.rs2()!=0||instr.func3()!=0)Error("Invalid instruction\n");
                        if(verbose) printf("fmv.d.x\t%s,%s", fRegName[instr.rd()], RegName[instr.rs1()]);
                        f[instr.rd()] = x[instr.rs1()];
                        counter("fmv.d.x");
						break;
                        
                    case 0b0010001:
                        switch (instr.func3()) {
                            case 0b0000://FSGNJ.D
                                if(verbose) printf("fsgnj.d\t%s,%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                                f[instr.rd()] = (f[instr.rs1()]&(~((uint64_t)1<<63)) ) | (f[instr.rs2()]& ((uint64_t)1<<63) );
                                counter("fsgnj.d");
								break;
                                
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                        
                    case 0b1010000:
                        switch (instr.func3()) {
                            case 0b001://FLT.S
                                if(verbose) printf("flt.s\t%s,%s,%s", RegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                                x[instr.rd()] = Reg(f[instr.rs1()]).f < Reg(f[instr.rs2()]).f;
                                counter("flt.s");
								break;
                                
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                        
                    case 0b1010001:
                        switch (instr.func3()) {
                            case 0b001://FLT.D
                                if(verbose) printf("flt.d\t%s,%s,%s", RegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                                x[instr.rd()] = Reg(f[instr.rs1()]).d < Reg(f[instr.rs2()]).d;
                                counter("flt.d");
								break;
                                
                            case 0b010://FEQ.D
                                if(verbose) printf("feq.d\t%s,%s,%s", RegName[instr.rd()], fRegName[instr.rs1()], fRegName[instr.rs2()]);
                                x[instr.rd()] = (f[instr.rs1()] == f[instr.rs2()]);
                                counter("feq.d");
								break;
                                
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                        
                    case 0b1101000:
                        switch (instr.rs2()) {
                            case 0b00010://FCVT.S.L
                                if(verbose) printf("fcvt.s.l\t%s,%s", fRegName[instr.rd()], RegName[instr.rs1()]);
                                f[instr.rd()] = Reg(float(uint64_t(int64_t(x[instr.rs1()])))).l;
                                counter("fcvt.s.l");
								break;
                                
                            case 0b00000://FCVT.S.W
                                if(verbose) printf("fcvt.s.w\t%s,%s", fRegName[instr.rd()], RegName[instr.rs1()]);
                                f[instr.rd()] = Reg(float(uint64_t(int64_t(int32_t(x[instr.rs1()]))))).l;
                                counter("fcvt.s.w");
								break;
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                    
                    case 0b01100001:
                        switch (instr.rs2()) {
                            case 0b00000://FCVT.W.D
                                if(verbose) printf("fcvt.w.d\t%s,%s", RegName[instr.rd()], fRegName[instr.rs1()]);
                                x[instr.rd()] = int64_t(int32_t(Reg(f[instr.rs1()]).d));
                                counter("fcvt.w.d");
								break;
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                        
                    case 0b01101001:
                        switch (instr.rs2()) {
                            case 0b00000://FCVT.D.W
                                if(verbose) printf("fcvt.d.w\t%s,%s", fRegName[instr.rd()], RegName[instr.rs1()]);
                                f[instr.rd()] = Reg(double(uint64_t(int64_t(int32_t(x[instr.rs1()]))))).l;
                                counter("fcvt.d.w");
								break;
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                        
                    case 0b0100000:
                        switch (instr.rs2()) {
                            case 0b00001://FCVT.S.D
                                if(verbose) printf("fcvt.s.d\t%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()]);
                                f[instr.rd()] = Reg(float(Reg(f[instr.rs1()]).d)).l;
                                counter("fcvt.s.d");
								break;
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                    
                    case 0b0100001:
                        switch (instr.rs2()) {
                            case 0b00000://FCVT.D.S
                                if(verbose) printf("fcvt.d.s\t%s,%s", fRegName[instr.rd()], fRegName[instr.rs1()]);
                                f[instr.rd()] = Reg(double(Reg(f[instr.rs1()]).f)).l;
                                counter("fcvt.d.s");
								break;
                                
                            default: Error("Invalid instruction\n");
                        }
                        break;
                    default: Error("Invalid instruction\n");
                }
                break;
                
            default: Error("Invalid instruction\n");
        }
        PC = PC_next;
        if(verbose)cout<<endl;
    }
}


struct	stat_riscv
{
    uint64_t	a;
    uint64_t	b;
    uint32_t	c;
    uint32_t	d;
    uint32_t	e;
    uint32_t	f;
    uint64_t	g;
    uint64_t	h;
    uint64_t    i;
    long        st_spare1;
    uint64_t    j;
    long		st_spare2;
    uint64_t	k;
    long		st_spare3;
    long		st_blksize;
    long		st_blocks;
    long        st_spare4[2];
};

vector< map<string,int>::iterator >v;
bool cmp(map<string,int>::iterator a,map<string,int>::iterator b)
{
    return a->second > b->second;
}
void printresult()
{
    cout<<"TOTAL:"<<" "<<totalcnt<<endl;
    map<string,int>::iterator it;
    for(it=instr_counter.begin();it!=instr_counter.end();it++)
    {
        v.push_back(it);
    }
    sort(v.begin(),v.end(),cmp);
    for(int i=0;i<20;i++)
    {
        it=v[i];
        cout<<it->first<<" "<<it->second<<endl;
    }
}
void ecall()
{
    switch (a7) {
        case 57:
            //a0=close(a0);
            break;
        case 62:
            a0=lseek(a0,a1,a2);
            break;
        case 63:
            a0=read(a0,(void*)mem.getPaddr(a1),a2);
            break;
        case 64:
            a0=write(a0,(const void*)mem.getPaddr(a1),a2);
            break;
        case 80:
        {
            struct stat tmp;
            a0=fstat(a0,&tmp);
            struct stat_riscv* ptr=(struct stat_riscv *)mem.getPaddr(a1);
            ptr->a=tmp.st_dev;
            ptr->b=tmp.st_ino;
            ptr->c=tmp.st_mode;
            ptr->d=tmp.st_nlink;
            ptr->e=tmp.st_uid;
            ptr->f=tmp.st_gid;
            ptr->g=tmp.st_rdev;
            ptr->h=tmp.st_size;
            ptr->i=tmp.st_atime;
            ptr->j=tmp.st_mtime;
            ptr->k=tmp.st_ctime;
        }
            break;
            
        case 93:
            if(verbose)printf("\n");
            printresult();
            exit(0);
            break;
        case 169:
            a0=gettimeofday((struct timeval*)mem.getPaddr(a0),NULL);
            break;
        case 214:
            //printf(" %lx" , mem.ReadDoubleword(gp-1960) );
            break;
        default:
            sprintf(ErrorMSG,"Undefined Ecall: %ld\n",a7);
            Error(ErrorMSG);
            break;
    }
}
