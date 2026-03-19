#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include "asr.h"

/*
 * 说明：
 * - 未定义 ASR_WITH_IFLYTEK_SDK：编译为 stub，避免缺少 SDK 头文件/库导致工程无法构建。
 * - 定义 ASR_WITH_IFLYTEK_SDK：启用讯飞离线语法识别实现（需自行在编译/链接中加入 SDK）。
 */

#ifdef ASR_WITH_IFLYTEK_SDK
#include <unistd.h>
#include <qisr.h>
#include <msp_cmn.h>
#include <msp_errors.h>
#endif

#define SAMPLE_RATE_16K     (16000)
#define SAMPLE_RATE_8K      (8000)
#define MAX_GRAMMARID_LEN   (32)
#define MAX_PARAMS_LEN      (1024)

/* 离线语法识别资源路径（工程内资源位于 bin/ 下） */
static const char *ASR_RES_PATH   = "fo|bin/msc/res/asr/common.jet";
/* 构建离线识别语法网络生成数据保存路径（工程内示例目录） */
static const char *GRM_BUILD_PATH = "bin/msc/res/asr/GrmBuilld";
/* 构建离线识别语法网络所用的语法文件（工程内示例文件） */
static const char *GRM_FILE       = "bin/call.bnf";
/* 更新离线识别语法的 contact 槽（示例语法 call.bnf） */
static const char *LEX_NAME       = "contact";

typedef struct _UserData {
	int     build_fini; //标识语法构建是否完成
	int     update_fini; //标识更新词典是否完成
	int     errcode; //记录语法构建或更新词典回调错误码
	char    grammar_id[MAX_GRAMMARID_LEN]; //保存语法构建返回的语法ID
}UserData;


static int parse_voice_id(const char *rec_rslt);

#ifdef ASR_WITH_IFLYTEK_SDK
static int build_grammar(UserData *udata); //构建离线识别语法网络
static int update_lexicon(UserData *udata); //更新离线识别语法词典
static int run_asr(UserData *udata, const char *pcm_path); //进行离线语法识别
#endif

/* 旧示例的交互式文件选择已移除：模块化后由调用者传入 pcm_path。 */

#ifdef ASR_WITH_IFLYTEK_SDK
static int build_grm_cb(int ecode, const char *info, void *udata)
{
	UserData *grm_data = (UserData *)udata;

	if (NULL != grm_data) {
		grm_data->build_fini = 1;
		grm_data->errcode = ecode;
	}

	if (MSP_SUCCESS == ecode && NULL != info) {
		printf("构建语法成功！ 语法ID:%s\n", info);
		if (NULL != grm_data)
			snprintf(grm_data->grammar_id, MAX_GRAMMARID_LEN - 1, info);
	}
	else
		printf("构建语法失败！%d\n", ecode);

	return 0;
}

static int build_grammar(UserData *udata)
{
	FILE *grm_file                           = NULL;
	char *grm_content                        = NULL;
	unsigned int grm_cnt_len                 = 0;
	char grm_build_params[MAX_PARAMS_LEN]    = {NULL};
	int ret                                  = 0;

	grm_file = fopen(GRM_FILE, "rb");	
	if(NULL == grm_file) {
		printf("打开\"%s\"文件失败！[%s]\n", GRM_FILE, strerror(errno));
		return -1; 
	}

	fseek(grm_file, 0, SEEK_END);
	grm_cnt_len = ftell(grm_file);
	fseek(grm_file, 0, SEEK_SET);

	grm_content = (char *)malloc(grm_cnt_len + 1);
	if (NULL == grm_content)
	{
		printf("内存分配失败!\n");
		fclose(grm_file);
		grm_file = NULL;
		return -1;
	}
	fread((void*)grm_content, 1, grm_cnt_len, grm_file);
	grm_content[grm_cnt_len] = '\0';
	fclose(grm_file);
	grm_file = NULL;

	snprintf(grm_build_params, MAX_PARAMS_LEN - 1, 
		"engine_type = local, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, ",
		ASR_RES_PATH,
		SAMPLE_RATE_16K,
		GRM_BUILD_PATH
		);
	ret = QISRBuildGrammar("bnf", grm_content, grm_cnt_len, grm_build_params, build_grm_cb, udata);

	free(grm_content);
	grm_content = NULL;

	return ret;
}

static int update_lex_cb(int ecode, const char *info, void *udata)
{
	UserData *lex_data = (UserData *)udata;

	if (NULL != lex_data) {
		lex_data->update_fini = 1;
		lex_data->errcode = ecode;
	}

	if (MSP_SUCCESS == ecode)
		printf("更新词典成功！\n");
	else
		printf("更新词典失败！%d\n", ecode);

	return 0;
}

static int update_lexicon(UserData *udata)
{
	const char *lex_content                   = "丁伟\n黄辣椒";
	unsigned int lex_cnt_len                  = strlen(lex_content);
	char update_lex_params[MAX_PARAMS_LEN]    = {NULL}; 

	snprintf(update_lex_params, MAX_PARAMS_LEN - 1, 
		"engine_type = local, text_encoding = UTF-8, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, grammar_list = %s, ",
		ASR_RES_PATH,
		SAMPLE_RATE_16K,
		GRM_BUILD_PATH,
		udata->grammar_id);
	return QISRUpdateLexicon(LEX_NAME, lex_content, lex_cnt_len, update_lex_params, update_lex_cb, udata);
}

#endif /* ASR_WITH_IFLYTEK_SDK */

static int g_voice_last_id = -1;

static int parse_int_after(const char *p)
{
	if (p == NULL)
		return -1;
	while (*p == ' ' || *p == '\t' || *p == '"' || *p == '\'')
		p++;
	if (*p < '0' || *p > '9')
		return -1;

	long value = 0;
	while (*p >= '0' && *p <= '9') {
		value = value * 10 + (*p - '0');
		if (value > INT_MAX)
			return -1;
		p++;
	}
	return (int)value;
}

static int parse_voice_id(const char *rec_rslt)
{
	if (rec_rslt == NULL)
		return -1;

	/* 兼容多种结果格式：id=123 / id="123" / <id>123</id> */
	const char *p = strstr(rec_rslt, "id=");
	if (p != NULL)
		return parse_int_after(p + 3);

	p = strstr(rec_rslt, "<id>");
	if (p != NULL)
		return parse_int_after(p + 4);

	return -1;
}

int voice_get_last_id(void)
{
	return g_voice_last_id;
}

#ifndef ASR_WITH_IFLYTEK_SDK

int voice_init(void)
{
	g_voice_last_id = -1;
	/* 未集成 SDK：返回失败，但保证工程可编译。 */
	return -1;
}

int voice_identify(const char *pcm_path)
{
	(void)pcm_path;
	g_voice_last_id = -1;
	return -1;
}

void voice_deinit(void)
{
	/* stub */
}

#else /* ASR_WITH_IFLYTEK_SDK */

static int run_asr(UserData *udata, const char *pcm_path)
{
	char asr_params[MAX_PARAMS_LEN]    = {NULL};
	const char *rec_rslt               = NULL;
	const char *final_rslt             = NULL;
	const char *session_id             = NULL;
	const char *asr_audiof             = NULL;
	FILE *f_pcm                        = NULL;
	char *pcm_data                     = NULL;
	long pcm_count                     = 0;
	long pcm_size                      = 0;
	int last_audio                     = 0;
	int aud_stat                       = MSP_AUDIO_SAMPLE_CONTINUE;
	int ep_status                      = MSP_EP_LOOKING_FOR_SPEECH;
	int rec_status                     = MSP_REC_STATUS_INCOMPLETE;
	int rss_status                     = MSP_REC_STATUS_INCOMPLETE;
	int errcode                        = -1;

	g_voice_last_id = -1;

	asr_audiof = (pcm_path != NULL) ? pcm_path : "bin/wav/1.pcm";
	f_pcm = fopen(asr_audiof, "rb");
	if (NULL == f_pcm) {
		printf("打开\"%s\"失败！[%s]\n", asr_audiof, strerror(errno));
		goto run_error;
	}
	fseek(f_pcm, 0, SEEK_END);
	pcm_size = ftell(f_pcm);
	fseek(f_pcm, 0, SEEK_SET);
	pcm_data = (char *)malloc(pcm_size);
	if (NULL == pcm_data)
		goto run_error;
	fread((void *)pcm_data, pcm_size, 1, f_pcm);
	fclose(f_pcm);
	f_pcm = NULL;

	//离线语法识别参数设置
	snprintf(asr_params, MAX_PARAMS_LEN - 1, 
		"engine_type = local, \
		asr_res_path = %s, sample_rate = %d, \
		grm_build_path = %s, local_grammar = %s, \
		result_type = xml, result_encoding = UTF-8, ",
		ASR_RES_PATH,
		SAMPLE_RATE_16K,
		GRM_BUILD_PATH,
		udata->grammar_id
		);
	session_id = QISRSessionBegin(NULL, asr_params, &errcode);
	if (NULL == session_id)
		goto run_error;
	printf("开始识别...\n");

	while (1) {
		unsigned int len = 6400;

		if (pcm_size < 12800) {
			len = pcm_size;
			last_audio = 1;
		}

		aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;

		if (0 == pcm_count)
			aud_stat = MSP_AUDIO_SAMPLE_FIRST;

		if (len <= 0)
			break;

		printf(">");
		fflush(stdout);
		errcode = QISRAudioWrite(session_id, (const void *)&pcm_data[pcm_count], len, aud_stat, &ep_status, &rec_status);
		if (MSP_SUCCESS != errcode)
			goto run_error;

		pcm_count += (long)len;
		pcm_size -= (long)len;

		//检测到音频结束
		if (MSP_EP_AFTER_SPEECH == ep_status)
			break;

		usleep(150 * 1000); //模拟人说话时间间隙
	}
	//主动点击音频结束
	QISRAudioWrite(session_id, (const void *)NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_status, &rec_status);

	free(pcm_data);
	pcm_data = NULL;

	//获取识别结果
	while (MSP_REC_STATUS_COMPLETE != rss_status && MSP_SUCCESS == errcode) {
		rec_rslt = QISRGetResult(session_id, &rss_status, 0, &errcode);
		if (rec_rslt != NULL)
			final_rslt = rec_rslt;
		usleep(150 * 1000);
	}
	printf("\n识别结束：\n");
	printf("=============================================================\n");
	if (final_rslt != NULL) {
		g_voice_last_id = parse_voice_id(final_rslt);
		printf("id = %d\n", g_voice_last_id);
	} else {
		printf("没有识别结果！\n");
	}
	printf("=============================================================\n");

	goto run_exit;

run_error:
	if (NULL != pcm_data) {
		free(pcm_data);
		pcm_data = NULL;
	}
	if (NULL != f_pcm) {
		fclose(f_pcm);
		f_pcm = NULL;
	}
run_exit:
	QISRSessionEnd(session_id, NULL);
	return errcode;
}

static UserData asr_data;

int voice_init(void)
{
	/* 允许通过编译宏覆盖 appid：-DASR_APPID=\"xxxx\" */
#ifndef ASR_APPID
#define ASR_APPID "5f62c65a"
#endif
	char login_config[64];
	snprintf(login_config, sizeof(login_config), "appid = %s", ASR_APPID);

	g_voice_last_id = -1;

	int ret = MSPLogin(NULL, NULL, login_config);
	if (MSP_SUCCESS != ret) {
		printf("MSPLogin 失败：%d\n", ret);
		return ret;
	}

	memset(&asr_data, 0, sizeof(UserData));
	printf("构建离线识别语法网络...\n");
	ret = build_grammar(&asr_data);
	if (MSP_SUCCESS != ret) {
		printf("构建语法调用失败：%d\n", ret);
		MSPLogout();
		return ret;
	}
	while (1 != asr_data.build_fini)
		usleep(300 * 1000);
	if (MSP_SUCCESS != asr_data.errcode) {
		printf("构建语法失败：%d\n", asr_data.errcode);
		MSPLogout();
		return asr_data.errcode;
	}
	return 0;
}

int voice_identify(const char *pcm_path)
{
	printf("开始离线语法识别...\n");
	int ret = run_asr(&asr_data, pcm_path);
	if (MSP_SUCCESS != ret) {
		printf("离线语法识别出错：%d\n", ret);
		return ret;
	}

	/* 词典更新不影响当前识别结果，按需启用；这里保持示例行为但不把失败视为致命。 */
	printf("更新离线语法词典...\n");
	ret = update_lexicon(&asr_data);
	if (MSP_SUCCESS != ret) {
		printf("更新词典调用失败：%d\n", ret);
		return ret;
	}
	while (1 != asr_data.update_fini)
		usleep(300 * 1000);
	if (MSP_SUCCESS != asr_data.errcode) {
		printf("更新词典失败：%d\n", asr_data.errcode);
		return asr_data.errcode;
	}

	return 0;
}

void voice_deinit(void)
{
	MSPLogout();
}

#endif /* ASR_WITH_IFLYTEK_SDK */
