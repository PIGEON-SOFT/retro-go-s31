#include "rg_system.h"
#include "translations.h"

static int rg_language = RG_LANG_EN;

static const char *zh_gettext(const char *text)
{
    static const struct { const char *en; const char *zh; } zh[] = {
        {"Never", "从不"},
        {"Always", "总是"},
        {"Yes", "是"},
        {"No", "否"},
        {"On", "开"},
        {"Off", "关"},
        {"Auto", "自动"},
        {"None", "无"},
        {"<None>", "<无>"},
        {"Language", "语言"},
        {"Language changed!", "语言已更改"},
        {"For these changes to take effect you must restart your device.\nrestart now?", "需要重启后生效。\n现在重启吗？"},
        {"Options", "选项"},
        {"About", "关于"},
        {"About Retro-Go", "关于 Retro-Go"},
        {"Reset", "重置"},
        {"Reset settings", "重置设置"},
        {"Reset all settings", "重置所有设置"},
        {"Reset all settings?", "重置所有设置？"},
        {"Reset Emulation?", "重置模拟器？"},
        {"Brightness", "亮度"},
        {"Volume", "音量"},
        {"Audio out", "音频输出"},
        {"Font type", "字体"},
        {"Scaling", "缩放"},
        {"Wi-Fi profile", "Wi-Fi 配置"},
        {"Wi-Fi Options", "Wi-Fi 选项"},
        {"Not connected", "未连接"},
        {"Connection failed!", "连接失败！"},
        {"Wi-Fi scan", "Wi-Fi 扫描"},
        {"No networks found", "没有找到网络"},
        {"Netplay", "联机"},
        {"Host Game (P1)", "主机游戏 (P1)"},
        {"Find Game (P2)", "查找游戏 (P2)"},
        {"ROMs not identical. Continue?", "ROM 不一致，继续？"},
        {"Resume game", "继续游戏"},
        {"New game", "新游戏"},
        {"Resume", "继续"},
        {"Hide tabs", "隐藏标签"},
        {"Hide", "隐藏"},
        {"Show", "显示"},
        {"Select file", "选择文件"},
        {"Place roms in folder: %s", "请把 ROM 放到：%s"},
        {"With file extension: %s", "文件扩展名：%s"},
        {"You can hide this tab in the menu", "可以在菜单中隐藏此标签"},
        {"Welcome to Retro-Go!", "欢迎使用 Retro-Go！"},
        {"List empty", "列表为空"},
        {"Name", "名称"},
        {"Artist", "作者"},
        {"Copyright", "版权"},
        {"Playing", "播放中"},
    };

    for (size_t i = 0; i < RG_COUNT(zh); ++i)
        if (strcmp(zh[i].en, text) == 0)
            return zh[i].zh;
    return NULL;
}

int rg_localization_get_language_id(void)
{
    return rg_language;
}

bool rg_localization_set_language_id(int language_id)
{
    if (language_id < 0 || language_id > RG_LANG_MAX - 1)
        return false;

    rg_language = language_id;
    return true;
}

const char *rg_gettext(const char *text)
{
    if (rg_language == 0 || text == NULL)
        return text; // If rg_language is english or text is NULL, we can return self

    if (rg_language == RG_LANG_ZH)
    {
        const char *msg = zh_gettext(text);
        if (msg)
            return msg;
    }

    for (size_t i = 0; i < RG_COUNT(translations); ++i)
    {
        if (strcmp(translations[i][0], text) == 0)
        {
            const char *msg = translations[i][rg_language];
            // If the translation is missing, we return the original string
            return msg ? msg : text;
        }
    }

    return text; // if no translation found
}

const char *rg_localization_get_language_name(int language_id)
{
    if (language_id < 0 || language_id > RG_LANG_MAX - 1)
        return NULL;
    return language_names[language_id];
}
