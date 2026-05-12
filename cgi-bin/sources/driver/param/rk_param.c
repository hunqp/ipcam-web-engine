#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

#include "rk_param.h"

dictionary *ini_d = NULL;

static char ini_path[128];
static pthread_mutex_t mt = PTHREAD_MUTEX_INITIALIZER;

int rk_param_init(char *user_ini_path) {
	assert(user_ini_path != NULL);

	pthread_mutex_lock(&mt);
	{
		memcpy(ini_path, user_ini_path, strlen(user_ini_path));
		ini_d = iniparser_load(ini_path);
		if (ini_d == NULL) {
			return -1;
		}
	}
	pthread_mutex_unlock(&mt);

	return 0;
}

int rk_param_savein() {
	FILE *fp = fopen(ini_path, "w");
	if (fp == NULL) {
		iniparser_freedict(ini_d);
		ini_d = NULL;
		return -1;
	}
	iniparser_dump_ini(ini_d, fp);
	fflush(fp);
	fclose(fp);

	return 0;
}

int rk_param_deinit() {
	assert(ini_d != NULL);

	pthread_mutex_lock(&mt);
	{
		rk_param_savein();
		iniparser_freedict(ini_d);
	}
	pthread_mutex_unlock(&mt);

	return 0;
}

int rk_param_reload() {
	int r = 0;
	assert(ini_d != NULL);

	pthread_mutex_lock(&mt);
	{
		iniparser_freedict(ini_d);
		ini_d = iniparser_load(ini_path);
		if (ini_d == NULL) {
			r = -1;
		}
	}
	pthread_mutex_unlock(&mt);

	return r;
}

int rk_param_get_int(const char *entry, int defaultValue) {
	int r = 0;

	pthread_mutex_lock(&mt);

	r = iniparser_getint(ini_d, entry, defaultValue);

	pthread_mutex_unlock(&mt);

	return r;
}

int rk_param_set_int(const char *entry, int val) {
	char chars[8];

	pthread_mutex_lock(&mt);

	memset(chars, 0, sizeof(chars));
	sprintf(chars, "%d", val);
	iniparser_set(ini_d, entry, chars);

	pthread_mutex_unlock(&mt);

	return 0;
}

char *rk_param_get_string(const char *entry, const char *defaultValue) {
	char *r = NULL;
	
	pthread_mutex_lock(&mt);

	r = (char*)iniparser_getstring(ini_d, entry, defaultValue);

	pthread_mutex_unlock(&mt);

	return r;
}

int rk_param_set_string(const char *entry, const char *val) {
	pthread_mutex_lock(&mt);

	iniparser_set(ini_d, entry, val);

	pthread_mutex_unlock(&mt);

	return 0;
}

