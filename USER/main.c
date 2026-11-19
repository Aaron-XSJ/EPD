#include "sys.h"
#include "delay.h"
#include "usart.h" 
#include "led.h"
#include "key.h"     
#include "usmart.h" 
#include "malloc.h"
#include "sdio_sdcard.h"  
#include "w25qxx.h"
#include "vs10xx.h"
//FATFS
#include "ff.h"
#include "exfuns.h"

extern  char audio_file_list[MAX_AUDIO_FILE_NUM][FN_BUF_LEN];
extern  uint16_t audio_file_cnt;

int main(void)
{	 
	u32 total,free;
//	u8 t=0;
	uint16_t idx;
	
	delay_init();	    	 //延时函数初始化
	delay_ms(1000);
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);//设置中断优先级分组为组2：2位抢占优先级，2位响应优先级
//	uart_init(115200);		//串口初始化为115200
//	usmart_dev.init(72);	//初始化USMART		
 	LED_Init();				//初始化与LED连接的硬件接口
	KEY_Init();				//初始化按键
	W25QXX_Init();			//初始化W25Q128
	VS_Init();
 	my_mem_init(SRAMIN);	//初始化内部内存池
//	Printf("FATFS TEST"); 
	while(SD_Init())//检测不到SD卡
	{
//		Printf("SD Card Error!");
		delay_ms(500);
		LED0=!LED0;//DS0闪烁
	}
 	exfuns_init();							//为fatfs相关变量申请内存				 
  	f_mount(fs[0],"0:",1);				//挂载SD卡
//	if(res==FR_OK)LED0=0;
// 	res=f_mount(fs[1],"1:",1); 				//挂载FLASH.	
//	if(res==0X0D)//FLASH磁盘,FAT文件系统错误,重新格式化FLASH
//	{
//		Printf("Flash Disk Formatting...");	//格式化FLASH
//		res=f_mkfs("1:",1,4096);//格式化FLASH,1,盘符;1,不需要引导区,8个扇区为1个簇
//		if(res==0)
//		{
//			f_setlabel((const TCHAR *)"1:ALIENTEK");	//设置Flash磁盘的名字为：ALIENTEK
//			Printf("Flash Disk Format Finish");	//格式化完成
//		}else ;//Printf("Flash Disk Format Error ");	//格式化失败
//		delay_ms(1000);
//	}		  
	while(exf_getfree("0",&total,&free))	//得到SD卡的总容量和剩余容量
	{
//		Printf("SD Card Fatfs Error!");
		delay_ms(500);
		LED0=!LED0;//DS0闪烁
	}
//	LED1=0;
//	VS_PlayFile("1.mp3");
//	VS_Sine_Test();
//	LED1=1;
// 	Printf("\ntotal:%dkb,free:%dkb",total,free);		    
	//扫描SD根目录音频
    audio_file_cnt = VS_ScanRootAudio();

    if(audio_file_cnt == 0)
    {
        while(1)
        {
            //没有找到音频文件，LED闪烁提示
            LED0=!LED0;
            delay_ms(300);
        }
    }

    while(1)
    {
        //循环播放（解码器复位已移入 VS_PlayFile，每曲开头执行）
        for(idx=0; idx<audio_file_cnt; idx++)
        {
            char full_path[20];
            //拼接完整路径 "0:/TEST.MP3"
			sprintf(full_path,"0:/%s", audio_file_list[idx]);
			VS_PlayFile(full_path);
        }
    }
}
















