#include <string.h>
#include "vs10xx.h"	
#include "delay.h" 	 
#include "flac.h" 
#include "usart.h"
#include "spi.h"
#include "ff.h"
#include "key.h"
#include "led.h"
//#include "print.h"

//按键状态标志
uint8_t g_next_song_flag = 0;   // 1=切下一曲
uint8_t g_play_pause_flag = 0;  // 0播放，1暂停

//保存根目录音频文件名列表
char audio_file_list[MAX_AUDIO_FILE_NUM][FN_BUF_LEN];
uint16_t audio_file_cnt = 0; //有效音频文件数量
 
//VS10XX设置参数
//0,高音控制；1，最低频限；2，低音控制；3，最低频限
//0,henh.1,hfreq.2,lenh.3,lfreq 5,主音量
u8 VS10XX_ID=0;
//便携小喇叭?：用 0x0044，+4dB 低音增益，防止小单元过载失真，保证人声清晰度
//桌面2.0音箱?：用 0x0077，+7dB 低音增益，强化低频氛围感，适合听带强律动的流行舞曲
//人声向流行歌?：用 0x0032，30Hz 低频仅 +3dB，完全突出人声，适合抒情慢歌
u8 VS10XXram[5]={0,0,3,2,230};
#define volt_down	0x0101
#define volt_base	0x2020
u16 volt = volt_base;

#define VS_CHUNK_SIZE  32
#define PLAY_BUF_SIZE 512
static uint8_t buf0[PLAY_BUF_SIZE];
static uint8_t buf1[PLAY_BUF_SIZE];

//软复位VS10XX
//注意,在这里设置完后,系统SPI时钟被修改成9M左右
void VS_Soft_Reset(void)
{	 
	u16 retry; 	   		   
	SPI1_SetSpeed(SPI_BaudRatePrescaler_256);   	//256分频 超低速 281.25Khz	
	retry = 0;
	while(VS_DQ == 0 && retry++ < 2000) delay_us(50);	//等待空闲（带超时，避免死等）
	SPI1_ReadWriteByte(0X00);       	//启动传输	 
	retry=0; 					
	VS10XX_ID=VS_RD_Reg(SPI_STATUS);//读取状态寄存器
	VS10XX_ID>>=4;					//得到VS10XX的芯片信号 
	
	if(VS10XX_ID==VS1053)VS_WR_Cmd(SPI_MODE,0x0816);//软件复位,新模式
	else VS_WR_Cmd(SPI_MODE,0x0804);				//软件复位,新模式

	while(VS_DQ==0&&retry<2000)			//等待DREQ为高
	{
		retry++;
		delay_us(50);
	};
	retry=0;
	while(VS_RD_Reg(SPI_CLOCKF)!=0X9800)//等待设置成功
	{
		VS_WR_Cmd(SPI_CLOCKF,0X9800);  	//设置VS10XX的时钟,3.5倍频 ,2xADD  
		if(retry++>1000)break; 
	}
	
 	VS_Rst_DecodeTime();				//复位解码时间	    
    //向VS10XX发送4个字节无效数据，用以启动SPI发送
 	SPI1_SetSpeed(EPD_SPI_BaudRate);			//8分频速度	  
    VS_XDCS=0;							//选中数据传输 记得,这里一定要传送0X00
	SPI1_ReadWriteByte(0X0);
	SPI1_ReadWriteByte(0X0);
	SPI1_ReadWriteByte(0X0);
	SPI1_ReadWriteByte(0X0);
	VS_XDCS=1;							//取消数据传输   								    
} 
//硬复位MP3
//返回1:复位失败
//返回0:复位成功
u8 VS_HD_Reset(void)
{
	u16 retry=0;
	VS_XDCS=1;	//取消数据传输
	VS_XCS=1;	//取消指令传输
	VS_RST=0;			    
	delay_ms(100);  
	VS_RST=1;    
	while(VS_DQ==0&&retry<2000)//等待DREQ为高
	{
		retry++;
		delay_us(50);
	};
	
	delay_ms(20);	
	if(retry>=2000)return 1;
	else return 0;	    		 
}

//正弦测试 
void VS_Sine_Test(void)
{											    
	VS_HD_Reset();	 
	VS_WR_Cmd(0x0b,0X2020);	  //设置音量	 
 	VS_WR_Cmd(SPI_MODE,0x0820);//进入VS10XX的测试模式     
	while(VS_DQ==0);     //等待DREQ为高
	//printf("mode sin:%x\n",VS_RD_Reg(SPI_MODE));
 	//向VS10XX发送正弦测试命令：0x53 0xef 0x6e n 0x00 0x00 0x00 0x00
 	//其中n = 0x24, 设定VS10XX所产生的正弦波的频率值，具体计算方法见VS10XX的datasheet
  	SPI1_SetSpeed(SPI_BaudRatePrescaler_64);//低速 
	VS_XDCS=0;//选中数据传输
	SPI1_ReadWriteByte(0x53);
	SPI1_ReadWriteByte(0xef);
	SPI1_ReadWriteByte(0x6e);
	SPI1_ReadWriteByte(0x24);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	delay_ms(200);
	VS_XDCS=1; 
    //退出正弦测试
    VS_XDCS=0;//选中数据传输
	SPI1_ReadWriteByte(0x45);
	SPI1_ReadWriteByte(0x78);
	SPI1_ReadWriteByte(0x69);
	SPI1_ReadWriteByte(0x74);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	delay_ms(100);
	VS_XDCS=1;		 

    //再次进入正弦测试并设置n值为0x44，即将正弦波的频率设置为另外的值
    VS_XDCS=0;//选中数据传输      
	SPI1_ReadWriteByte(0x53);
	SPI1_ReadWriteByte(0xef);
	SPI1_ReadWriteByte(0x6e);
	SPI1_ReadWriteByte(0x44);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	delay_ms(200);
	VS_XDCS=1;
    //退出正弦测试
    VS_XDCS=0;//选中数据传输
	SPI1_ReadWriteByte(0x45);
	SPI1_ReadWriteByte(0x78);
	SPI1_ReadWriteByte(0x69);
	SPI1_ReadWriteByte(0x74);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	delay_ms(100);
	VS_XDCS=1;	 
} 	  
//VS10XX ram 测试 	
//返回值:
//对1053:0X83FF,表明全部测试OK
//对1003:0x807F,表明全部测试OK																			 
u16 VS_Ram_Test(void)
{		
	u16 RAM_Read;				 
	VS_HD_Reset();     		   
 	VS_WR_Cmd(SPI_MODE,0x0820);		// 进入VS10XX的RAM测试模式
	while (VS_DQ==0); 				// 等待DREQ为高	   
	SPI1_SetSpeed(SPI_BaudRatePrescaler_256);   //256分频速度	     
	VS_XDCS=0;												   	 
	SPI1_ReadWriteByte(0x4d);
	SPI1_ReadWriteByte(0xea);
	SPI1_ReadWriteByte(0x6d);
	SPI1_ReadWriteByte(0x54);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	SPI1_ReadWriteByte(0x00);
	VS_XDCS=1;	  
	delay_ms(200);  
	RAM_Read=VS_RD_Reg(SPI_HDAT0);
	// 如果得到的值为0x807F，则表明完好。			 
	printf("SPI_HDAT0:%x\n",RAM_Read);//输出结果 
	return RAM_Read;
}		   
//向VS10XX写命令
//address:命令地址
//data:命令数据
void VS_WR_Cmd(u8 address,u16 data)
{
	u16 retry=0;
    while(VS_DQ==0&&(retry++)<0xfffe)retry++;	//等待空闲    
	SPI1_SetSpeed(SPI_BaudRatePrescaler_64);    			//64分频速度	     
	VS_XDCS=1; 
	VS_XCS=0;  	   
	SPI1_ReadWriteByte(VS_WRITE_COMMAND);//发送VS10XX的写命令
	SPI1_ReadWriteByte(address); //地址
	SPI1_ReadWriteByte(data>>8); //发送高八位
	SPI1_ReadWriteByte(data);	 //第八位
	VS_XCS=1;
	delay_us(50);
	SPI1_SetSpeed(EPD_SPI_BaudRate);
} 
//向VS10XX写数据
void VS_WR_Data(u8 data)
{
	VS_XDCS=0;
	SPI1_ReadWriteByte(data);
	VS_XDCS=1;
}         
//读VS10XX的寄存器     
//注意不要用倍速读取,会出错
u16 VS_RD_Reg(u8 address)
{
	u16 temp=0; 
	u16 retry=0;
    while(VS_DQ==0&&(retry++)<2000);	//等待空闲	  
	if(retry>=2000)return 0;	 						    
	SPI1_SetSpeed(SPI_BaudRatePrescaler_64);		//64分频速度	
	VS_XDCS=1;       
	VS_XCS=0;       
	SPI1_ReadWriteByte(VS_READ_COMMAND);//发送VS10XX的读命令
	SPI1_ReadWriteByte(address);        //地址
	temp=SPI1_ReadWriteByte(0xff);		//读取高字节
	temp=temp<<8;
	temp+=SPI1_ReadWriteByte(0xff); 	//读取低字节
	VS_XCS=1;      
	SPI1_SetSpeed(EPD_SPI_BaudRate);			//8分频速度	
    return temp; 
}  
//读取VS10XX的RAM
u16 VS_RD_Wram(u16 addr) 
{
	VS_WR_Cmd(SPI_WRAMADDR, addr);
	return VS_RD_Reg(SPI_WRAM);
} 
  
//FOR WAV HEAD0 :0X7761 HEAD1:0X7665    
//FOR MIDI HEAD0 :other info HEAD1:0X4D54
//FOR WMA HEAD0 :data speed HEAD1:0X574D
//FOR MP3 HEAD0 :data speed HEAD1:ID
//比特率预定值
const u16 bitrate[2][16]=
{ 
{0,8,16,24,32,40,48,56,64,80,96,112,128,144,160,0}, 
{0,32,40,48,56,64,80,96,112,128,160,192,224,256,320,0}
};
//返回Kbps的大小
//得到mp3&wma的波特率
u16 VS_Get_HeadInfo(void)
{
	unsigned int HEAD0;
	unsigned int HEAD1;            
    HEAD0=VS_RD_Reg(SPI_HDAT0); 
    HEAD1=VS_RD_Reg(SPI_HDAT1);
    switch(HEAD1)
    {        
        case 0x7665:return 0;	//WAV格式
        case 0X4D54:			//MIDI格式 
		case 0X4154:			//AAC_ADTS
		case 0X4144:			//AAC_ADIF
		case 0X4D34:			//AAC_MP4/M4A
		case 0X4F67:			//OGG
        case 0X574D:			//WMA格式
        {
			////printf("HEAD0:%d\n",HEAD0);
            HEAD1=HEAD0*2/25;					//相当于*8/100
            if((HEAD1%10)>5)return HEAD1/10+1;	//对小数点第一位四舍五入
            else return HEAD1/10;
        }
        default://MP3格式
        {
            HEAD1>>=3;
            HEAD1=HEAD1&0x03; 
            if(HEAD1==3)HEAD1=1;
            else HEAD1=0;
            return bitrate[HEAD1][HEAD0>>12];
        }
    } 
}  
//重设解码时间                          
void VS_Rst_DecodeTime(void)
{
	VS_WR_Cmd(SPI_DECODE_TIME,0x0000);
	VS_WR_Cmd(SPI_DECODE_TIME,0x0000);//操作两次
}
//得到mp3的播放时间n sec
u16 VS_Get_DecodeTime(void)
{ 
	return VS_RD_Reg(SPI_DECODE_TIME);   
} 	 

//加载用户代码到VS10XX
//FLAC解码.但是加载后不支持WMA.
//写入plugin  
void VS_Load_FlacPatch(void) 
{	    	 
	u16 i; 
	u16 addr, n, val; 	  			   
	for(i=0;i<VS1053B_PATCHLEN;) 
	{ 
		addr = vs1053b_patch[i++]; 
		n    = vs1053b_patch[i++]; 
		if(n & 0x8000U) //RLE run, replicate n samples 
		{ 
			n  &= 0x7FFF; 
			val = vs1053b_patch[i++]; 
			while(n--)VS_WR_Cmd(addr, val);  
		}else //copy run, copy n sample 
		{ 
			while(n--) 
			{ 
				val = vs1053b_patch[i++]; 
				VS_WR_Cmd(addr, val); 
			} 
		} 
	} 		   
}  	     		   
//设定VS10XX播放的音量和高低音 
void set10XX(void)
{
    u8 t;
    u16 bass=0; //暂存音调寄存器值

	//0,henh.1,hfreq.2,lenh.3,lfreq        
    for(t=0;t<4;t++)
    {
        bass<<=4;
        bass+=VS10XXram[t]; 
    }     
	VS_WR_Cmd(SPI_BASS,bass);//BASS   
    VS_WR_Cmd(SPI_VOL,volt); //设音量 
} 
//初始化VS10XX的IO口	 
void VS_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA|RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOE,ENABLE);
	
	//PB0--DREQ
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_0;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;//上拉输入
	GPIO_Init(GPIOB, &GPIO_InitStructure);
	//PE11--RST
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_11;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;//推挽输出
	GPIO_Init(GPIOE, &GPIO_InitStructure);
	//PA8--XCS	PA4--XDCS
	GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_8|GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;//推挽输出
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	SPI1_Init();								//初始化SPI
	VS_XDCS=1;
	VS_XCS=1;
} 	 

/**
 * @brief 用 0x00 冲刷 VS10XX 的 SDI FIFO
 * @param zero_bytes 零字节数：VS1003 = 2048，VS1053 = 2052
 * @note  按 32 字节分块并逐块等待 DREQ，避免 FIFO 溢出；
 *        带超时保护，芯片异常时不会死等
 */
static void VS_FlushFifo(u16 zero_bytes)
{
    u16 sent = 0;

    SPI1_SetSpeed(EPD_SPI_BaudRate);
    while(sent < zero_bytes)
    {
        u16 chunk;
        u16 k;
        u16 retry = 0;

        chunk = zero_bytes - sent;
        if(chunk > VS_CHUNK_SIZE) chunk = VS_CHUNK_SIZE;

        while(VS_DQ == 0)
        {
            delay_us(20);
            if(++retry > 10000) return; /* 约 200ms 超时 */
        }

        VS_XDCS = 0;
        for(k = 0; k < chunk; k++) SPI1_ReadWriteByte(0x00);
        VS_XDCS = 1;
        sent += chunk;
    }
}

/**
 * @brief 曲目结束：取消解码 + 清空 SDI FIFO + 复位解码时间
 * @note  原实现只发 32 字节 0x00，FIFO 里残留的解码样本会在切歌时
 *        形成尾音；这里按数据手册补满整个 FIFO 深度。
 *        VS1003 的 MODE bit3 是 SM_OUTOFWAV，故 SM_CANCEL 仅用于 VS1053。
 */
static void VS_EndOfSong(void)
{
    if(VS10XX_ID == VS1053)
    {
        u16 retry = 0;
        VS_WR_Cmd(SPI_MODE, VS_RD_Reg(SPI_MODE) | SM_CANCEL);
        while((VS_RD_Reg(SPI_MODE) & SM_CANCEL) && retry++ < 200) delay_us(100);
    }

    VS_FlushFifo((VS10XX_ID == VS1053) ? 2052 : 2048);
    VS_Rst_DecodeTime();
}

void VS_PlayFile(const char *filename)
{
    FIL file;
    FRESULT res;
    UINT bytes_read;
    u8 key;
    uint8_t *tmp;
    uint8_t *send_buf = buf0;
    uint8_t *fill_buf = buf1;
    uint8_t  eof_flag  = 0;
    uint16_t send_len  = 0;
    uint16_t send_ptr  = 0;
    uint16_t can_send;
    uint16_t k;

    /* 每首歌开头都复位解码器：清掉上一曲残留的解码状态与 SDI FIFO，
       避免切歌尾音；复位后重新写时钟、BASS 与音量 */
    VS_HD_Reset();
    VS_Soft_Reset();
    set10XX();

    res = f_open(&file, filename, FA_READ);
    if(res != FR_OK)
    {
        //printf("open fail %d\n",res);
        return;
    }

    //预读第一块
    res = f_read(&file, send_buf, PLAY_BUF_SIZE, &bytes_read);
    send_len = bytes_read;
    send_ptr = 0;
    eof_flag = (res != FR_OK || bytes_read == 0) ? 1 : 0;

	g_next_song_flag = 0; //进入播放，清除切歌标志

    while(1)
    {
		//========按键扫描========
        key = KEY_Scan(0);
        if(key == WKUP_PRES)
        {
            //下一曲
            g_next_song_flag = 1;
        }
        else if(key == KEY0_PRES)
        {
            //翻转暂停标志
            g_play_pause_flag = !g_play_pause_flag;
			if(g_play_pause_flag == 1)
			{
				if(volt<0xffff-volt_down)volt += volt_down;
				else volt = volt_base;
				VS_WR_Cmd(SPI_VOL,volt);
			}
        }

        //按下下一曲，直接跳出当前歌曲
        if(g_next_song_flag == 1)
        {
            break;
        }

        //暂停状态，不发送数据，只轮询按键
        if(g_play_pause_flag == 1)
        {
            delay_ms(10);
            continue;
        }
        // ========= 当VS1003 FIFO满(DREQ==0)，CPU空闲，此时预读SD卡掩盖读取耗时 =========
        if(VS_DQ == 0)
        {
            if(!eof_flag && send_len == 0)
            {
                // DREQ低，VS在解码，趁机读取SD到fill_buf
                res = f_read(&file, fill_buf, PLAY_BUF_SIZE, &bytes_read);
                if(res != FR_OK || bytes_read == 0)
                {
                    eof_flag = 1;
                }
                //交换缓冲
                tmp = send_buf;
                send_buf = fill_buf;
                fill_buf = tmp;

                send_len = bytes_read;
                send_ptr = 0;
            }
            delay_us(20);
            continue;
        }

        // DREQ ==1，可以发送数据
        if(send_len > 0)
        {
            VS_XDCS = 0;
            can_send = send_len - send_ptr;
            if(can_send > VS_CHUNK_SIZE)
                can_send = VS_CHUNK_SIZE;

            for(k=0;k<can_send;k++)
            {
                SPI1_ReadWriteByte(send_buf[send_ptr++]);
            }
            VS_XDCS = 1;

            // 当前缓冲区发完
            if(send_ptr >= send_len)
            {
                send_len = 0;
            }
        }
        else
        {
            // send_len==0，但是DREQ为1，此时DREQ空闲但是没有数据，需要读SD
            if(!eof_flag)
            {
                res = f_read(&file, fill_buf, PLAY_BUF_SIZE, &bytes_read);
                if(res != FR_OK || bytes_read == 0)
                {
                    eof_flag = 1;
                }
                tmp = send_buf;
                send_buf = fill_buf;
                fill_buf = tmp;
                send_len = bytes_read;
                send_ptr = 0;
            }
        }

        if(eof_flag && send_len == 0)
        {
            break;
        }
    }
    f_close(&file);

    /* 收尾：取消解码 + 补零冲刷 FIFO + 清解码时间（替代原来的 32 字节零） */
    VS_EndOfSong();
}

/**
 * @brief 扫描SD根目录，收集mp3/wav音频文件名到全局列表
 * @retval 成功返回文件个数，0代表没找到音频
 */
uint16_t VS_ScanRootAudio(void)
{
    FRESULT res;
    DIR dir;
    FILINFO fno;
    char *fn;

    audio_file_cnt = 0;

    //打开根目录 "0:/"
    res = f_opendir(&dir, "0:/");
    if(res != FR_OK)
    {
        return 0;
    }

    for(;;)
    {
        res = f_readdir(&dir, &fno);
        if(res != FR_OK || fno.fname[0] == 0)
        {
            break; //目录读完
        }
        //跳过子目录，只处理普通文件
        if( (fno.fattrib & AM_DIR) )
        {
            continue;
        }

        fn = fno.fname;

        //判断后缀 8.3短文件名，后缀是大写 .MP3 .WAV
        if( (strstr(fn,".MP3")!=NULL) || (strstr(fn,".WAV")!=NULL) )
        {
            if(audio_file_cnt < MAX_AUDIO_FILE_NUM)
            {
                strcpy(audio_file_list[audio_file_cnt], fn);
                audio_file_cnt++;
            }
        }
    }
    f_closedir(&dir);
    return audio_file_cnt;
}
