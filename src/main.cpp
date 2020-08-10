#include <mbed.h>
#include "GT20L16J1Y_font.h"
#include <locale.h>
#include <cwchar>

// LCD
BusInOut LcdData(D2, D3, D4, D5, D6, D7, D8, D9); // LCDデータバス
DigitalOut Lcd_WR(D10);                           // ~書き込み信号(0=Active)
DigitalOut Lcd_RD(D11);                           // ~読み込み信号(0=Active)
DigitalOut Lcd_CE(D12);                           // ~チップセレクト(0=Active)
DigitalOut LcdCommandData(D13);                   // レジスタ選択(1=コマンド、0=データ)
DigitalOut Lcd_Reset(D14);                        // ~リセット(0=Reset)

// キーパッド
BusIn PadRow(PC_4, PB_13, PB_14, PC_15, PB_1, PB_2, PB_12); // 行
BusOut PadCol(A1, A2, A3, A4);                              // 列
DigitalOut PadBackLight(PA_11);                             // バックライト
DigitalOut PadIndicator(PA_12);                             // LED

GT20L16J1Y_FONT CgRom(PC_12, PC_11, PC_10, PA_15);

#define DISPLAY_WIDTH 30  // ディスプレイ桁数(8*nドット)
#define DISPLAY_HEIGHT 16 // ディスプレイ行数(8*n ドット)

#define VRAM_START 0x0000                                                        // VRAM開始アドレス
#define VRAM_TEXT_ADDR VRAM_START                                                // テキスト開始アドレス
#define VRAM_GRPH_ADDR (VRAM_TEXT_ADDR + (DISPLAY_WIDTH * DISPLAY_HEIGHT))       // グラフィック開始アドレス
#define VRAM_END (VRAM_GRPH_ADDR + ((DISPLAY_WIDTH * (DISPLAY_HEIGHT * 8)) * 2)) // VRAM終了アドレス
#define CGRAM_START ((VRAM_END & 0xF800) + 0x1800)                               // CGRAM開始アドレス
#define CGRAM_STORE_OFFSET 0x400                                                 // 文字コード0x80以降に置くためのオフセット
#define CGRAM_COUNT 0x800                                                        // CGRAMに登録する文字数
#define CGRAM_END (CGRAM_START + CGRAM_STORE_OFFSET + (8 * CGRAM_COUNT))         // CGRAM終了アドレス

void reset();
union statusCode statusRead();
void waitForWrite();
void waitForAutoRead();
void waitForAutoWrite();
void dataWrite2Bytes(unsigned char command, unsigned char ldata, unsigned char hdata);
void dataWriteByte(unsigned char command, unsigned char data);
void autoDataRead(unsigned char *dataArray, int length);
void autoDataWrite(unsigned char *dataArray, int length);
unsigned char dataRead(unsigned char command);
void memoryClear(int from, int to);
void commandSet(unsigned char command);
void lcdPutc(char chr);
void lcdPuts(unsigned char *str);
void read2BytesCg(unsigned char *cgData, unsigned short code);
void writeKanjiStr(char *str);

union statusCode {
    unsigned int usData;
    struct
    {
        unsigned int comEn : 1;  // コマンド実行可能か？ 1=実行可 0=実行不可
        unsigned int rwEn : 1;   // データリード・ライト可能か？ 1=実行可 0=実行不可
        unsigned int aRD : 1;    // オートデータリード可能か？ 1=実行可 0=実行不可
        unsigned int aWR : 1;    // オートデータライト可能か？ 1=実行可 0=実行不可
        unsigned int dummy : 1;  // 未使用
        unsigned int lcdcEn : 1; // LCDC動作可能か？ 1=動作可 0=動作不可
        unsigned int error : 1;  // エラーフラグ 1=エラー 0=正常
        unsigned int blink : 1;  // ブリンクフラグ 1=表示 0=非表示
    } tBit;
};

union convIntByte {
    int iData;
    unsigned char ucData[2];
};

union convUShortByte {
    unsigned short usData;
    unsigned char ucData[2];
};

enum Commands
{
    // レジスタセット
    REG_CURSOR = 0x21, // カーソルポインタセット X,Y
    REG_OFFSET,        // オフセットレジスタセット Data,00H
    REG_ADDR = 0x24,   // アドレスポインタセット lData,HData

    // 画面設定
    DISP_TEXT_HOME_ADDR = 0x40, // 表示テキストホームアドレスセット lData,HData
    DISP_TEXT_WIDTH,            // テキストエリア桁数指定 Data,00H
    DISP_GRPH_HOME_ADDR,        // 表示グラフィックホームアドレスセット lData,HData
    DISP_GRPH_WIDTH,            // グラフィックエリア桁数指定 Data,00H

    // モードセット
    MODE_SET = 0x80,       // モードセット
    MODE_OR = 0x00,        // 表示ORモード MODE_SETに足し込むこと
    MODE_EXOR = 0x01,      // 表示EXORモード MODE_SETに足し込むこと
    MODE_AND = 0x03,       // 表示ANDモード MODE_SETに足し込むこと
    MODE_TEXT_ATTR = 0x04, // テキストアトリビュート MODE_SETに足し込むこと
    MODE_INT_CG = 0x00,    // 内部CGモード MODE_SETに足し込むこと
    MODE_EXT_CG = 0x08,    // 外部CGモード MODE_SETに足し込むこと

    // 表示イネーブル
    ENA_BASE = 0x90,            // 表示イネーブル 単体使用で"表示禁止"
    ENA_CURSOR_NOTBLINK = 0x02, // カーソル表示ブリンク禁止 ENA_BASEに足し込むこと
    ENA_CURSOR_BLINK = 0x03,    // カーソル表示ブリンクあり ENA_BASEに足し込むこと
    ENA_TEXTONLY = 0x04,        // テキスト表示グラフィック禁止  ENA_BASEに足し込むこと
    ENA_GRPHONLY = 0x08,        // グラフィック表示テキスト禁止 ENA_BASEに足し込むこと
    ENA_TEXTGRPH = 0x0C,        // テキスト・グラフィック表示   ENA_BASEに足し込むこと

    // カーソル指定
    CURSOR_BASE = 0xA0, // n+1ラインカーソル (n=0～7(1ライン～8ライン)、足し込む)

    // オート指定
    AUTO_WRITE = 0xB0, // データオートライトセット
    AUTO_READ,         // データオートリードセット
    AUTO_RESET,        // オートリセット(オート指定解除)

    // データＲ／Ｗ
    DATA_WRITE_UP = 0xC0, // データライトアップ data
    DATA_READ_UP,         // データリードアップ
    DATA_WRITE_DOWN,      // データライトダウン data
    DATA_READ_DOWN,       // データリードダウン
    DATA_WRITE,           // データライト data
    DATA_READ,            // データリード

    // スクリーンピーク(多分使えない)
    SCR_PEEK = 0xE0,

    // スクリーンコピー(多分使えない)
    SCR_COPY = 0xE8

};

int main()
{

    union statusCode stcd;
    union convIntByte conv;
    unsigned char writeData[0x100];
    unsigned char stringsData[16][31] =
        {
            //       123456789012345678901234567890
            /* 0 */ "",
            /* 1 */ "  This is a test program for  ",
            /* 2 */ "",
            /* 3 */ " W01L-6102-E106/01            ",
            /* 4 */ "   <LCD Panel + Key Pad Unit> ",
            /* 5 */ {0xA0, 0xA1, 0xA4, 0xA5, 0xA8, 0xA9, 0xAC, 0xAD, 0xB0, 0xB1, 0xB4, 0xB5, 0xB8, 0xB9, 0xBC, 0xBD, 0xC0, 0xC1, 0xC4, 0xC5, 0xC8, 0xC9, 0xCC, 0xCD, 0xD0, 0xD1, 0xD4, 0xD5, 0xD8, 0xD9, 0x00},
            /* 6 */ {0xA2, 0xA3, 0xA6, 0xA7, 0xAA, 0xAB, 0xAE, 0xAF, 0xB2, 0xB3, 0xB6, 0xB7, 0xBA, 0xBB, 0xBE, 0xBF, 0xC2, 0xC3, 0xC6, 0xC7, 0xCA, 0xCB, 0xCE, 0xCF, 0xD2, 0xD3, 0xD6, 0xD7, 0xDA, 0xDB, 0x00},
            /* 7 */ "   H e l l o ,  W o r l d !   ",
        };

    // unsigned char toshiba[8][8] = {
    //     {0x01, 0x01, 0xff, 0x01, 0x3f, 0x21, 0x3f, 0x21},
    //     {0x00, 0x00, 0xff, 0x00, 0xfc, 0x04, 0xfc, 0x04},
    //     {0x21, 0x3f, 0x05, 0x0d, 0x19, 0x31, 0xe1, 0x01},
    //     {0x04, 0xfc, 0x40, 0x60, 0x30, 0x1c, 0x07, 0x00},

    //     {0x08, 0x08, 0xff, 0x08, 0x09, 0x01, 0x01, 0x7f},
    //     {0x10, 0x10, 0xff, 0x10, 0x10, 0x00, 0x00, 0xfc},
    //     {0x00, 0x00, 0x00, 0x01, 0x07, 0x3c, 0xe7, 0x00},
    //     {0x18, 0x30, 0x60, 0xc0, 0x00, 0x00, 0xe0, 0x3f},
    // };

    int i, j;
    char writeChr[32];

    stcd.usData = 0;

    // put your setup code here, to run once:

    for (i = 0; i < 0x100; i++)
    {
        writeData[i] = i;
    }

    for (i = 0; i < 16; i += 2)
    {
        for (j = 0; j < 30; j += 2)
        {
            stringsData[i][j] = (30 * i + 2 * j);
            stringsData[i][j + 1] = (30 * i + 2 * j) + 1;
            stringsData[i + 1][j] = (30 * i + 2 * j) + 2;
            stringsData[i + 1][j + 1] = (30 * i + 2 * j) + 3;
        }
    }
    reset();
    waitForWrite();

    dataWrite2Bytes(REG_CURSOR, 0, 0);
    dataWrite2Bytes(REG_ADDR, 0, 0);
    conv.iData = VRAM_TEXT_ADDR;
    dataWrite2Bytes(DISP_TEXT_HOME_ADDR, conv.ucData[0], conv.ucData[1]);
    dataWrite2Bytes(DISP_TEXT_WIDTH, DISPLAY_WIDTH, 0);
    conv.iData = VRAM_GRPH_ADDR;
    dataWrite2Bytes(DISP_GRPH_HOME_ADDR, conv.ucData[0], conv.ucData[1]);
    dataWrite2Bytes(DISP_GRPH_WIDTH, DISPLAY_WIDTH, 0);
    conv.iData = CGRAM_START >> 11;
    dataWrite2Bytes(REG_OFFSET, conv.ucData[0], conv.ucData[1]);

    commandSet(MODE_SET + MODE_OR + MODE_INT_CG);
    commandSet(ENA_BASE + ENA_TEXTGRPH);
    commandSet(CURSOR_BASE + 3);

    memoryClear(VRAM_START, VRAM_END);

    conv.iData = CGRAM_START + CGRAM_STORE_OFFSET /*  + CGRAM_STORE_OFFSET */;
    //conv.iData = VRAM_GRPH_ADDR;
    dataWrite2Bytes(REG_ADDR, conv.ucData[0], conv.ucData[1]);

    //                 123456789012345678901234567890
    strncpy(writeChr, "あいうえおかきくけこさしすせそ", sizeof(writeChr));
    writeKanjiStr(writeChr);

    strncpy(writeChr, "たちつてとなにぬねのはひふへほ", sizeof(writeChr));
    writeKanjiStr(writeChr);

    strncpy(writeChr, "まみむめもやゆよらりるれろわん", sizeof(writeChr));
    writeKanjiStr(writeChr);

    strncpy(writeChr, "をぁぃぅぇぉっゃゅょ゛゜　　　", sizeof(writeChr));
    writeKanjiStr(writeChr);

    // CgRom.read(0x938c);
    // autoDataWrite(CgRom.bitmap, 32);
    // CgRom.read(0x8ec5);
    // autoDataWrite(CgRom.bitmap, 32);

    conv.iData = VRAM_TEXT_ADDR;
    dataWrite2Bytes(REG_ADDR, conv.ucData[0], conv.ucData[1]);
    for (i = 0; i < 16; i++)
    {
        conv.iData = 0x1e * (i + 1);
        autoDataWrite(writeData + (i * 0x10), 0x10);
        dataWrite2Bytes(REG_ADDR, conv.ucData[0], conv.ucData[1]);
    }

    thread_sleep_for(3000);
    memoryClear(VRAM_TEXT_ADDR, VRAM_GRPH_ADDR);
    conv.iData = VRAM_TEXT_ADDR;
    dataWrite2Bytes(REG_ADDR, conv.ucData[0], conv.ucData[1]);
    for (i = 0; i < 16; i++)
    {
        if (i == 8)
        {
            thread_sleep_for(2000);

            conv.iData = (CGRAM_START >> 11) + 1;
            dataWrite2Bytes(REG_OFFSET, conv.ucData[0], conv.ucData[1]);
        }
        conv.iData = 0x1e * ((i + 1));
        autoDataWrite(stringsData[i], 30);
        dataWrite2Bytes(REG_ADDR, conv.ucData[0], conv.ucData[1]);
        if (i == 15)
        {
            thread_sleep_for(2000);

            conv.iData = (CGRAM_START >> 11);
            dataWrite2Bytes(REG_OFFSET, conv.ucData[0], conv.ucData[1]);
        }
    }

    /* for (i = 0; i < 0x80; i++)
    {
        dataWriteByte(0xC0, writeData[i]);
    }
 */
    while (1)
    {
        // put your main code here, to run repeatedly:
        stcd.usData = statusRead().usData;
        //printf("%d\n",stcd.usData);
        PadIndicator = ((stcd.tBit.comEn && stcd.tBit.lcdcEn && stcd.tBit.rwEn) ? 1 : 0);
        Lcd_RD = 1;
        Lcd_WR = 1;
    }
}

void reset()
{
    // printf("Resetting...");
    fflush(stdout);
    Lcd_Reset = 0;

    LcdData.output();
    LcdCommandData = 0;
    LcdData = 0;
    Lcd_WR = 1;
    Lcd_RD = 1;

    PadCol = 0;
    PadBackLight = 1;
    PadIndicator = 1;

    Lcd_CE = 1;
    thread_sleep_for(5);
    Lcd_Reset = 1;
    thread_sleep_for(5);
    fflush(stdout);
    PadBackLight = 0;
    PadIndicator = 0;
    // printf("Done!\n");
}

union statusCode statusRead()
{
    union statusCode stcd;
    stcd.usData = 0;

    LcdData.input();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 0;
    Lcd_WR = 1;
    stcd.usData = LcdData.read();
    Lcd_RD = 1;
    Lcd_CE = 1;
    Lcd_WR = 1;

    LcdData.output();
    // printf("%2x ", stcd.usData);
    return stcd;
}

void waitForWrite()
{
    union statusCode stcd;

    // printf("Waiting for write...");
    fflush(stdout);

    while (1)
    {
        stcd.usData = statusRead().usData;

        if ((stcd.tBit.comEn == 1 && /* stcd.tBit.lcdcEn == 1 && */ stcd.tBit.rwEn == 1))
            break;
    }
    // printf("Done!\n");
    fflush(stdout);
}

void waitForAutoWrite()
{
    union statusCode stcd;
    // printf("Waiting for auto write...");
    fflush(stdout);

    while (1)
    {

        stcd.usData = statusRead().usData;
        if ((stcd.tBit.comEn == 1 && /* stcd.tBit.lcdcEn == 1 &&  */ stcd.tBit.aWR == 1))
            break;
    }
    // printf("Done!\n");
    fflush(stdout);
}
void waitForAutoRead()
{
    union statusCode stcd;
    // printf("Waiting for auto read...");
    fflush(stdout);
    while (1)
    {
        stcd.usData = statusRead().usData;
        if ((stcd.tBit.comEn == 1 && /*  stcd.tBit.lcdcEn == 1 &&  */ stcd.tBit.aRD == 1))
            break;
    }
    // printf("Done!\n");
    fflush(stdout);
}

void dataWrite2Bytes(unsigned char command, unsigned char ldata, unsigned char hdata)
{
    // printf("Data Write CMD=0x%02x DATA=0x%02x 0x%02x\n", command, hdata, ldata);
    fflush(stdout);
    waitForWrite();
    LcdCommandData = 0;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = ldata;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    waitForWrite();
    LcdCommandData = 0;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = hdata;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    waitForWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = command;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;
}

void dataWriteByte(unsigned char command, unsigned char data)
{
    // printf("Data Write CMD=0x%02x DATA=0x%02x\n", command, data);
    fflush(stdout);
    waitForWrite();
    LcdCommandData = 0;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = data;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    waitForWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = command;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;
}

void autoDataRead(unsigned char *dataArray, int length)
{
    int i;
    union convIntByte conv;

    waitForWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = AUTO_READ;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    for (i = 0; i < length; i++)
    {
        waitForAutoRead();
        LcdCommandData = 0;
        Lcd_CE = 0;
        Lcd_RD = 0;
        Lcd_WR = 1;
        LcdData.input();
        conv.iData = LcdData.read();
        LcdData.output();
        dataArray[i] = conv.ucData[0];
        Lcd_CE = 1;
        Lcd_RD = 1;
        Lcd_WR = 1;
    }
    waitForAutoRead();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = AUTO_RESET;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    // printf("Read data is...\n");

    // for (i = 0; i < length; i++)
    // {
    //     printf("%02x ", dataArray[i]);
    //     if (i % 16 == 0)
    //         printf("\n");
    // }
    // printf("\n");
}

void autoDataWrite(unsigned char *dataArray, int length)
{
    int i;
    waitForWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = AUTO_WRITE;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    for (i = 0; i < length; i++)
    {
        waitForAutoWrite();
        // printf("Write data is... %02x\n", dataArray[i]);

        LcdCommandData = 0;
        Lcd_CE = 0;
        Lcd_RD = 1;
        Lcd_WR = 0;
        LcdData = dataArray[i];
        Lcd_CE = 1;
        Lcd_RD = 1;
        Lcd_WR = 1;
    }
    waitForAutoWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = AUTO_RESET;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;
}
void memoryClear(int from, int to)
{
    int i;
    union convIntByte conv;
    // printf("Memory Clear...");

    conv.iData = from;

    dataWrite2Bytes(REG_ADDR, conv.ucData[0], conv.ucData[1]);
    waitForWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = AUTO_WRITE;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    for (i = 0; i < (to - from); i++)
    {
        //printf("addr:%04x  ",i);
        waitForAutoWrite();
        LcdCommandData = 0;
        Lcd_CE = 0;
        Lcd_RD = 1;
        Lcd_WR = 0;
        LcdData = 0;
        Lcd_CE = 1;
        Lcd_RD = 1;
        Lcd_WR = 1;
    }
    waitForAutoWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = AUTO_RESET;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;
}

unsigned char dataRead(unsigned char command)
{
    union convIntByte conv;
    waitForWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = command;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    waitForWrite();
    LcdCommandData = 0;
    Lcd_CE = 0;
    Lcd_RD = 0;
    Lcd_WR = 1;
    LcdData.input();
    conv.iData = LcdData.read();
    LcdData.output();
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;

    // printf("Data Write CMD=0x%02x DATA=0x%02x\n", command, conv.ucData[0]);

    return conv.ucData[0];
}

void lcdPutc(char chr)
{
    dataWriteByte(DATA_WRITE_UP, chr >= ' ' ? chr - ' ' : 0);
}

void lcdPuts(unsigned char *str)
{
    unsigned int i = 0;
    for (i = 0; i < strlen((char *)str); i++)
    {
        dataWriteByte(DATA_WRITE_UP, str[i] >= ' ' ? str[i] - ' ' : 0);
    }
}

void commandSet(unsigned char command)
{
    // printf("Sending Command CMD=0x%02x\n", command);
    waitForWrite();
    LcdCommandData = 1;
    Lcd_CE = 0;
    Lcd_RD = 1;
    Lcd_WR = 0;
    LcdData = command;
    Lcd_CE = 1;
    Lcd_RD = 1;
    Lcd_WR = 1;
}

union BitConverter {
    unsigned char ucData;
    struct
    {
        unsigned char bit0 : 1;
        unsigned char bit1 : 1;
        unsigned char bit2 : 1;
        unsigned char bit3 : 1;
        unsigned char bit4 : 1;
        unsigned char bit5 : 1;
        unsigned char bit6 : 1;
        unsigned char bit7 : 1;
    } tBit;
};
void read2BytesCg(unsigned char *cgData, unsigned short code)
{
    union BitConverter bc;
    int i, j;

    // printf("! read2BytesData\n");
    for (i = 0; i < 32; i++)
    {
        cgData[i] = 0;
    }

    CgRom.read(code);
    for (i = 0; i < 4; i++)
    {
        // for (j = 0; j < 8; j++)
        // {
        for (j = 0; j < 8; j++)
        {
            bc.ucData = CgRom.bitmap[i * 8 + (7 - j)];
            cgData[i * 8 + 0] += bc.tBit.bit0 << j;
            cgData[i * 8 + 1] += bc.tBit.bit1 << j;
            cgData[i * 8 + 2] += bc.tBit.bit2 << j;
            cgData[i * 8 + 3] += bc.tBit.bit3 << j;
            cgData[i * 8 + 4] += bc.tBit.bit4 << j;
            cgData[i * 8 + 5] += bc.tBit.bit5 << j;
            cgData[i * 8 + 6] += bc.tBit.bit6 << j;
            cgData[i * 8 + 7] += bc.tBit.bit7 << j;
        }
        // }
    }
}

void writeKanjiStr(char *str)
{
    unsigned int i;
    union convUShortByte conv;
    unsigned char cgData[32];
    // char buf[256];
    // utf8tosjis(str,strlen(str),buf,sizeof(buf));

    for (i = 0; i < strlen(str); i += 2)
    {
        conv.ucData[1] = str[i];
        conv.ucData[0] = str[i + 1];
        read2BytesCg(cgData, conv.usData);
        autoDataWrite(cgData, 32);
    }
}