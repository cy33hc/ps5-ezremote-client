#ifndef __SCE_IME_DIALOG_H__
#define __SCE_IME_DIALOG_H__

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

extern "C"
{
int sceImeDialogInit(const SceImeDialogParam*, void*);
int sceImeDialogGetResult(SceImeDialogResult*);
int sceImeDialogTerm(void);

SceImeDialogStatus sceImeDialogGetStatus(void);
};

#endif