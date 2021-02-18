#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <math.h>
#include "bsp.h"
#include "sysctl.h"
#include "plic.h"
#include "utils.h"
#include "gpiohs.h"
#include "fpioa.h"
#include "lcd.h"
#include "nt35310.h"
#include "dvp.h"
#include "ov5640.h"
#include "ov2640.h"
#include "gc0328.h"
#include "uarths.h"
#include "kpu.h"
#include "region_layer.h"
#include "image_process.h"
#include "w25qxx.h"
#include "board_config.h"

#define 	CLASS_NUMBER 			2
#define 	ANCHOR_NUM 				5  	//5 or 9
#define  	LOAD_KMODEL_FROM_FLASH 	0


#if ANCHOR_NUM == 5
#define KMODEL_SIZE (543 * 1024)
static float anchor[ANCHOR_NUM * 2] = {0.156250, 0.222548,0.361328, 0.489583,0.781250, 0.983133,1.621094, 1.964286,3.574219, 3.940000};
#elif ANCHOR_NUM == 9
#define KMODEL_SIZE (571 * 1024)
static float anchor[ANCHOR_NUM * 2] = {0.117188, 0.166667,0.224609, 0.312500,0.390625, 0.531250, 0.664062, 0.838196,0.878906, 1.485714,1.269531, 1.125714, 1.728516, 2.096633,2.787402, 3.200000,4.382959, 4.555469};
#endif

#if LOAD_KMODEL_FROM_FLASH
uint8_t model_data[KMODEL_SIZE];
#else

#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#define INCBIN_PREFIX
#include "incbin.h"

#if ANCHOR_NUM == 5
INCBIN(model, "detect_5.kmodel");
#elif ANCHOR_NUM == 9
INCBIN(model, "detect_9.kmodel");
#endif

#endif

volatile uint32_t g_ai_done_flag;
volatile uint8_t g_dvp_finish_flag;
static image_t kpu_image, display_image;

kpu_model_context_t obj_detect_task;
static region_layer_t obj_detect_rl;
static obj_info_t obj_detect_info;


static void ai_done(void *ctx)
{
    g_ai_done_flag = 1;
}

static int dvp_irq(void *ctx)
{
    if (dvp_get_interrupt(DVP_STS_FRAME_FINISH))
    {
        dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 0);
        dvp_clear_interrupt(DVP_STS_FRAME_FINISH);
        g_dvp_finish_flag = 1;
    }
    else
    {
        dvp_start_convert();
        dvp_clear_interrupt(DVP_STS_FRAME_START);
    }
    return 0;
}

static void io_mux_init(void)
{
#if (BOARD_VERSION == BOARD_V1_2_LE)
	/* Init DVP IO map and function settings */
	fpioa_set_function(DVP_RST_PIN, FUNC_CMOS_RST);
	fpioa_set_function(DVP_PWDN_PIN, FUNC_CMOS_PWDN);
	fpioa_set_function(DVP_XCLK_PIN, FUNC_CMOS_XCLK);
	fpioa_set_function(DVP_VSYNC_PIN, FUNC_CMOS_VSYNC);
	fpioa_set_function(DVP_HREF_PIN, FUNC_CMOS_HREF);
	fpioa_set_function(DVP_PCLK_PIN, FUNC_CMOS_PCLK);
	fpioa_set_function(DVP_SCCB_SCLK_PIN, FUNC_SCCB_SCLK);
	fpioa_set_function(DVP_SCCB_SDA_PIN, FUNC_SCCB_SDA);
	sysctl_set_spi0_dvp_data(1);

	/* Init LCD IO map and function settings */
	fpioa_set_function(LCD_DC_PIN, FUNC_GPIOHS0 + LCD_DC_IO);
	fpioa_set_function(LCD_CS_PIN, FUNC_SPI0_SS3);
	fpioa_set_function(LCD_RW_PIN, FUNC_SPI0_SCLK);
    fpioa_set_function(LCD_BLIGHT_PIN, FUNC_GPIOHS0 + LCD_BLIGHT_IO);
    gpiohs_set_drive_mode(LCD_BLIGHT_IO, GPIO_DM_OUTPUT);
    gpiohs_set_pin(LCD_BLIGHT_IO, GPIO_PV_LOW);

	fpioa_set_function(LCD_RST_PIN, FUNC_GPIOHS0 + LCD_RST_IO);
	gpiohs_set_drive_mode(LCD_RST_IO, GPIO_DM_OUTPUT);
	gpiohs_set_pin(LCD_RST_IO, GPIO_PV_HIGH);
	
#elif (BOARD_VERSION == BOARD_V1_3)

	/* Init DVP IO map and function settings */
	fpioa_set_function(DVP_PWDN_PIN, FUNC_CMOS_PWDN);
	fpioa_set_function(DVP_XCLK_PIN, FUNC_CMOS_XCLK);
	fpioa_set_function(DVP_VSYNC_PIN, FUNC_CMOS_VSYNC);
	fpioa_set_function(DVP_HREF_PIN, FUNC_CMOS_HREF);
	fpioa_set_function(DVP_PCLK_PIN, FUNC_CMOS_PCLK);
	fpioa_set_function(DVP_SCCB_SCLK_PIN, FUNC_SCCB_SCLK);
	fpioa_set_function(DVP_SCCB_SDA_PIN, FUNC_SCCB_SDA);
	sysctl_set_spi0_dvp_data(1);	

	/* Init LCD IO map and function settings */
	fpioa_set_function(LCD_DC_PIN, FUNC_GPIOHS0 + LCD_DC_IO);
	fpioa_set_function(LCD_CS_PIN, FUNC_SPI0_SS3);
	fpioa_set_function(LCD_RW_PIN, FUNC_SPI0_SCLK);
	fpioa_set_function(LCD_BLIGHT_PIN, FUNC_GPIOHS0 + LCD_BLIGHT_IO);
	gpiohs_set_drive_mode(LCD_BLIGHT_IO, GPIO_DM_OUTPUT);	
	gpiohs_set_pin(LCD_BLIGHT_IO, GPIO_PV_LOW);
	
	fpioa_set_function(LCD_RST_PIN, FUNC_GPIOHS0 + LCD_RST_IO);
	gpiohs_set_drive_mode(LCD_RST_IO, GPIO_DM_OUTPUT);	
	gpiohs_set_pin(LCD_RST_IO, GPIO_PV_HIGH);

#elif (BOARD_VERSION == BOARD_KD233)
    /* Init DVP IO map and function settings */
    fpioa_set_function(DVP_RST_PIN, FUNC_CMOS_RST);
    fpioa_set_function(DVP_PWDN_PIN, FUNC_CMOS_PWDN);
    fpioa_set_function(DVP_XCLK_PIN, FUNC_CMOS_XCLK);
    fpioa_set_function(DVP_VSYNC_PIN, FUNC_CMOS_VSYNC);
    fpioa_set_function(DVP_HREF_PIN, FUNC_CMOS_HREF);
    fpioa_set_function(DVP_PCLK_PIN, FUNC_CMOS_PCLK);
    fpioa_set_function(DVP_SCCB_SCLK_PIN, FUNC_SCCB_SCLK);
    fpioa_set_function(DVP_SCCB_SDA_PIN, FUNC_SCCB_SDA);
    sysctl_set_spi0_dvp_data(1);

    /* Init SPI IO map and function settings */
    fpioa_set_function(LCD_DC_PIN, FUNC_GPIOHS0 + LCD_DC_IO);
    fpioa_set_function(LCD_CS_PIN, FUNC_SPI0_SS3);
    fpioa_set_function(LCD_RW_PIN, FUNC_SPI0_SCLK);

#endif
}

static void io_set_power(void)
{
#if (BOARD_VERSION == BOARD_V1_2_LE) || (BOARD_VERSION == BOARD_V1_3)
	/* Set dvp and spi pin to 1.8V */
	sysctl_set_power_mode(SYSCTL_POWER_BANK0, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK1, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK2, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK3, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK4, SYSCTL_POWER_V33);
	sysctl_set_power_mode(SYSCTL_POWER_BANK5, SYSCTL_POWER_V33);
	
	/* Set dvp and spi pin to 1.8V */
	sysctl_set_power_mode(SYSCTL_POWER_BANK6, SYSCTL_POWER_V18);
	sysctl_set_power_mode(SYSCTL_POWER_BANK7, SYSCTL_POWER_V18);	

#elif 	(BOARD_VERSION == BOARD_KD233)
    /* Set dvp and spi pin to 1.8V */
    sysctl_set_power_mode(SYSCTL_POWER_BANK0, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK1, SYSCTL_POWER_V18);
    sysctl_set_power_mode(SYSCTL_POWER_BANK2, SYSCTL_POWER_V18);
#endif
}


typedef struct
{
	char *str;
	uint16_t color;
	uint16_t height;
	uint16_t width;
	uint32_t *ptr;
} class_lable_t;

class_lable_t class_lable[CLASS_NUMBER] =
{
	{ "no_mask", NAVY },
	{ "mask", DARKGREEN }
};

static uint32_t lable_string_draw_ram[115 * 16 * 8 / 2];


static void lable_init(void)
{
	uint8_t index;

	class_lable[0].height = 16;
	class_lable[0].width = 8 * strlen(class_lable[0].str);
	class_lable[0].ptr = lable_string_draw_ram;
	lcd_ram_draw_string(class_lable[0].str, class_lable[0].ptr, BLACK, class_lable[0].color);
	for (index = 1; index < CLASS_NUMBER; index++) {
		class_lable[index].height = 16;
		class_lable[index].width = 8 * strlen(class_lable[index].str);
		class_lable[index].ptr = class_lable[index - 1].ptr + class_lable[index - 1].height * class_lable[index - 1].width / 2;
		lcd_ram_draw_string(class_lable[index].str, class_lable[index].ptr, BLACK, class_lable[index].color);
	}
}

static void drawboxes(uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t class, float prob)
{
	if (x1 >= 320)
		x1 = 319;
	if (x2 >= 320)
		x2 = 319;
	if (y1 >= 240)
		y1 = 239;
	if (y2 >= 240)
		y2 = 239;

	lcd_draw_rectangle(x1, y1, x2, y2, 2, class_lable[class].color);
	lcd_draw_picture(x1 + 1, y1 + 1, class_lable[class].width, class_lable[class].height, class_lable[class].ptr);
}

static void draw_edge(uint32_t *gram, obj_info_t *obj_info, uint32_t index, uint16_t color)
{
    uint32_t data = ((uint32_t)color << 16) | (uint32_t)color;
    uint32_t *addr1, *addr2, *addr3, *addr4, x1, y1, x2, y2;

    x1 = obj_info->obj[index].x1;
    y1 = obj_info->obj[index].y1;
    x2 = obj_info->obj[index].x2;
    y2 = obj_info->obj[index].y2;

    if (x1 <= 0)
        x1 = 1;
    if (x2 >= 319)
        x2 = 318;
    if (y1 <= 0)
        y1 = 1;
    if (y2 >= 239)
        y2 = 238;

    addr1 = gram + (320 * y1 + x1) / 2;
    addr2 = gram + (320 * y1 + x2 - 8) / 2;
    addr3 = gram + (320 * (y2 - 1) + x1) / 2;
    addr4 = gram + (320 * (y2 - 1) + x2 - 8) / 2;
    for (uint32_t i = 0; i < 4; i++)
    {
        *addr1 = data;
        *(addr1 + 160) = data;
        *addr2 = data;
        *(addr2 + 160) = data;
        *addr3 = data;
        *(addr3 + 160) = data;
        *addr4 = data;
        *(addr4 + 160) = data;
        addr1++;
        addr2++;
        addr3++;
        addr4++;
    }
    addr1 = gram + (320 * y1 + x1) / 2;
    addr2 = gram + (320 * y1 + x2 - 2) / 2;
    addr3 = gram + (320 * (y2 - 8) + x1) / 2;
    addr4 = gram + (320 * (y2 - 8) + x2 - 2) / 2;
    for (uint32_t i = 0; i < 8; i++)
    {
        *addr1 = data;
        *addr2 = data;
        *addr3 = data;
        *addr4 = data;
        addr1 += 160;
        addr2 += 160;
        addr3 += 160;
        addr4 += 160;
    }
}

int main(void)
{
    /* Set CPU and dvp clk */
    sysctl_pll_set_freq(SYSCTL_PLL0, 800000000UL);
    sysctl_pll_set_freq(SYSCTL_PLL1, 400000000UL);
    sysctl_clock_enable(SYSCTL_CLOCK_AI);
    uarths_init();
    io_set_power();
    io_mux_init();
    plic_init();
	lable_init();
	
    /* flash init */
    printf("flash init\n");
    w25qxx_init(3, 0);
    w25qxx_enable_quad_mode();
	
#if LOAD_KMODEL_FROM_FLASH
    w25qxx_read_data(0xA00000, model_data, KMODEL_SIZE, W25QXX_QUAD_FAST);
#endif

    /* LCD init */
    printf("LCD init\n");
    lcd_init();
    lcd_set_direction(LCD_XYZ);

    lcd_clear(BLACK);
	lcd_draw_string(136, 70, "DEMO", WHITE);
	lcd_draw_string(104, 150, "face mask detection", WHITE);
    /* DVP init */
    printf("DVP init\n");
    dvp_init(CAMERA_REG_LENGTH);
    dvp_set_xclk_rate(CAMERA_XCLK_RATE);
    dvp_enable_burst();
    dvp_set_image_format(DVP_CFG_RGB_FORMAT);
    dvp_set_image_size(320, 240);
    dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 0);
    dvp_disable_auto();
    printf("DVP interrupt config\n");
    plic_set_priority(IRQN_DVP_INTERRUPT, 1);
    plic_irq_register(IRQN_DVP_INTERRUPT, dvp_irq, NULL);
    plic_irq_enable(IRQN_DVP_INTERRUPT);

#if OV2640
    ov2640_init();
#elif OV5640
	ov5640_init();
#elif GC0328
    gc0328_init();
    open_gc0328_1();
#endif

    kpu_image.pixel = 3;
    kpu_image.width = 320;
    kpu_image.height = 256;
    image_init(&kpu_image);
    memset(kpu_image.addr, 127, kpu_image.pixel * kpu_image.width * kpu_image.height);
    dvp_set_ai_addr((uint32_t)(kpu_image.addr + 8 * 320), (uint32_t)(kpu_image.addr + 320 * 256 + 8 * 320), (uint32_t)(kpu_image.addr + 320 * 256 * 2 + 8 * 320));
    dvp_set_output_enable(0, 1);
	
    display_image.pixel = 2;
    display_image.width = 320;
    display_image.height = 240;
    image_init(&display_image);   
    dvp_set_display_addr((uint32_t)display_image.addr);
    dvp_set_output_enable(1, 1);

    /* init obj detect model */
    if (kpu_load_kmodel(&obj_detect_task, model_data) != 0)
    {
        printf("\nmodel init error\n");
        while (1);
    }
    obj_detect_rl.anchor_number = ANCHOR_NUM;
    obj_detect_rl.anchor = anchor;
    obj_detect_rl.threshold = 0.7;
    obj_detect_rl.nms_value = 0.4;
    obj_detect_rl.classes = CLASS_NUMBER;
    region_layer_init(&obj_detect_rl, 10, 8, (4 + 2 + 1) * ANCHOR_NUM, kpu_image.width, kpu_image.height);

    /* enable global interrupt */
    sysctl_enable_irq();
	
    /* system start */
    printf("System start\n");

    while (1)
    {
        g_dvp_finish_flag = 0;
        dvp_clear_interrupt(DVP_STS_FRAME_START | DVP_STS_FRAME_FINISH);
        dvp_config_interrupt(DVP_CFG_START_INT_ENABLE | DVP_CFG_FINISH_INT_ENABLE, 1);
        while (g_dvp_finish_flag == 0)
            ;
        /* run obj detect */
        g_ai_done_flag = 0;
        kpu_run_kmodel(&obj_detect_task, kpu_image.addr, DMAC_CHANNEL5, ai_done, NULL);
        while(!g_ai_done_flag);
        float *output;
        size_t output_size;
        kpu_get_output(&obj_detect_task, 0, (uint8_t **)&output, &output_size);
        obj_detect_rl.input = output;
        region_layer_run(&obj_detect_rl, &obj_detect_info);
		/* display pic*/
		lcd_draw_picture(0, 0, 320, 240, display_image.addr);

		/* draw boxs */
		region_layer_draw_boxes(&obj_detect_rl, drawboxes); 
		
    }
}
