#ifndef YD_I18N_H
#define YD_I18N_H

#ifdef __cplusplus
extern "C" {
#endif

extern const char** YD_languages_list;
extern int YD_languages_list_len;

const char *YD_get_phrase_str(const char *lang, const char *phrase);
const char *YD_language_to_formal_str(const char *lang);
int YD_init_translations();
void YD_deinit_translations();

#ifdef __cplusplus
}
#endif

#endif /* YD_I18N_H */