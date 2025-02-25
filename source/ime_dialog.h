/*
  VitaShell
  Copyright (C) 2015-2018, TheFloW

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef __EZ_IME_DIALOG_H__
#define __EZ_IME_DIALOG_H__

#define IME_DIALOG_RESULT_NONE 0
#define IME_DIALOG_RESULT_RUNNING 1
#define IME_DIALOG_RESULT_FINISHED 2
#define IME_DIALOG_RESULT_CANCELED 3

#define IME_DIALOG_ALREADY_RUNNING -1

// #include <orbis/ImeDialog.h>
typedef enum SceImeDialogStatus
{
    SCE_IME_DIALOG_STATUS_NONE,
    SCE_IME_DIALOG_STATUS_RUNNING,
    SCE_IME_DIALOG_STATUS_FINISHED
} SceImeDialogStatus;

typedef int (*SceImeTextFilter)(wchar_t*, uint32_t*, const wchar_t*, uint32_t);

typedef enum SceImeDialogType
{
	SCE_IME_TYPE_DEFAULT,
	SCE_IME_TYPE_BASIC_LATIN,
	SCE_IME_TYPE_URL,
	SCE_IME_TYPE_MAIL,
	SCE_IME_TYPE_NUMBER
} SceImeDialogType;

typedef enum SceImeDialogEnterLabel
{
	SCE_IME_ENTER_LABEL_DEFAULT,
	SCE_IME_ENTER_LABEL_SEND,
	SCE_IME_ENTER_LABEL_SEARCH,
	SCE_IME_ENTER_LABEL_GO
} SceImeDialogEnterLabel;

typedef enum SceImeDialogInputMethod
{
	SCE_IME_INPUT_METHOD_DEFAULT
} SceImeDialogInputMethod;

typedef enum SceImeDialogHalign
{
	SCE_IME_HALIGN_LEFT,
	SCE_IME_HALIGN_CENTER,
	SCE_IME_HALIGN_RIGHT
} SceImeDialogHalign;

typedef enum SceImeDialogValign
{
	SCE_IME_VALIGN_TOP,
	SCE_IME_VALIGN_CENTER,
	SCE_IME_VALIGN_BOTTOM
} SceImeDialogValign;

typedef enum SceImeDialogEndStatus
{
	SCE_IME_DIALOG_END_STATUS_OK,
	SCE_IME_DIALOG_END_STATUS_USER_CANCELED,
	SCE_IME_DIALOG_END_STATUS_ABORTED
} SceImeDialogEndStatus;

typedef struct SceImeDialogParam
{
    int userId;
    SceImeDialogType type;
    uint64_t supportedLanguages;
    SceImeDialogEnterLabel enterLabel;
    SceImeDialogInputMethod inputMethod;
    SceImeTextFilter filter;
    uint32_t option;
    uint32_t maxTextLength;
    wchar_t *inputTextBuffer;
    float posx;
    float posy;
    SceImeDialogHalign halign;
    SceImeDialogValign valign;
    const wchar_t *placeholder;
    const wchar_t *title;
    int8_t reserved[16];
} SceImeDialogParam;

typedef struct SceImeDialogResult
{
  SceImeDialogEndStatus outcome;
  int8_t reserved[12];
} SceImeDialogResult;

typedef void (*ime_callback_t)(int ime_result);

namespace Dialog
{
  int initImeDialog(const char *Title, const char *initialTextBuffer, int max_text_length, SceImeDialogType type, float posx, float posy);
  uint8_t *getImeDialogInputText();
  uint16_t *getImeDialogInputText16();
  const char *getImeDialogInitialText();
  int isImeDialogRunning();
  int updateImeDialog();
}

#endif
