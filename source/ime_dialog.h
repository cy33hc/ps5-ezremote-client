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

#include "sceImeDialog.h"
#include "sceUserService.h"

#define IME_DIALOG_RESULT_NONE 0
#define IME_DIALOG_RESULT_RUNNING 1
#define IME_DIALOG_RESULT_FINISHED 2
#define IME_DIALOG_RESULT_CANCELED 3

#define IME_DIALOG_ALREADY_RUNNING -1

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
