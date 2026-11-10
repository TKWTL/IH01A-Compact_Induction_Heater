/*
 * This file is part of the MiaoUI Library.
 *
 * Copyright (c) 2025, JFeng-Z, <2834294740@qq.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Created on: 2025-02-08
 */
#include "widget/text.h"
#include "core/ui.h"
#include "display/dispDriver.h"
#include "string.h"

/* UTF-8-safe paragraph wrapping based on actual glyph advance widths.
 * No malloc, fixed stack buffer; never split a UTF-8 code point. */
#include "i18n.h"
void Text_Widget(ui_t *ui)
{
    const char *ptr;
    const uint8_t *font;
    char line[128];
    unsigned len = 0u;
    int row = 0;
    const int left = 4, right = UI_HOR_RES - 5;
    int font_height;
    if(!ui || !ui->nowItem || !ui->nowItem->element || !ui->nowItem->element->text) return;
    ptr=IH_I18n_Text(ui->nowItem->element->text->ptr);
    font=IH_I18n_Font();
    font_height=ui->nowItem->element->text->fontHight;
    Disp_SetFont(font);
    if(!Dialog_Show(ui,1,1,UI_HOR_RES-2,UI_VER_RES-2)) return;
    while(*ptr && ((row+1)*font_height < UI_VER_RES-2))
    {
        unsigned cbytes=1u;
        unsigned i;
        if(*ptr=='\n')
        {
            line[len]='\0';
            Disp_DrawUTF8(left,font_height*(row+1),line);
            ++row;len=0u;++ptr;
            continue;
        }
        if(((unsigned char)*ptr&0xf0u)==0xe0u)cbytes=3u;
        else if(((unsigned char)*ptr&0xe0u)==0xc0u)cbytes=2u;
        else if(((unsigned char)*ptr&0xf8u)==0xf0u)cbytes=4u;
        for(i=0u;i<cbytes;i++)if(ptr[i]=='\0')break;
        cbytes=i;
        if(cbytes==0u)break;
        if(len+cbytes>=sizeof(line))
        {
            line[len]='\0';Disp_DrawUTF8(left,font_height*(row+1),line);
            ++row;len=0u;
            if((row+1)*font_height>=UI_VER_RES-2)break;
        }
        for(i=0u;i<cbytes;i++)line[len+i]=ptr[i];
        line[len+cbytes]='\0';
        if(len && Disp_GetUTF8Width(line)>(unsigned)(right-left))
        {
            line[len]='\0';
            Disp_DrawUTF8(left,font_height*(row+1),line);
            ++row;len=0u;
            if((row+1)*font_height>=UI_VER_RES-2)break;
            for(i=0u;i<cbytes;i++)line[i]=ptr[i];
            line[cbytes]='\0';
        }
        len+=cbytes;ptr+=cbytes;
    }
    if(len && (row+1)*font_height<UI_VER_RES-2)
    {
        line[len]='\0';Disp_DrawUTF8(left,font_height*(row+1),line);
    }
    Disp_SendBuffer();
}
