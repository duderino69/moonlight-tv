#include "app.h"

#include "pref_obj.h"

#include "ui/settings/settings.controller.h"
#include "stream/platform.h"

#include "util/i18n.h"

typedef struct decoder_pane_t {
    lv_fragment_t base;
    settings_controller_t *parent;

    lv_obj_t *hdr_checkbox;
    lv_obj_t *hdr_hint;

    pref_dropdown_string_entry_t vdec_entries[DECODER_COUNT + 1];
    int vdec_entries_len;

    pref_dropdown_string_entry_t adec_entries[AUDIO_COUNT + 1];
    int adec_entries_len;

    pref_dropdown_int_entry_t surround_entries[3];
    int surround_entries_len;
} decoder_pane_t;

static lv_obj_t *create_obj(lv_fragment_t *self, lv_obj_t *view);

static void pane_ctor(lv_fragment_t *self, void *args);

static void pref_mark_restart_cb(lv_event_t *e);

static void hdr_state_update_cb(lv_event_t *e);

static void hdr_more_click_cb(lv_event_t *e);

const lv_fragment_class_t settings_pane_decoder_cls = {
        .constructor_cb = pane_ctor,
        .create_obj_cb = create_obj,
        .instance_size = sizeof(decoder_pane_t),
};

static void pane_ctor(lv_fragment_t *self, void *args) {
    decoder_pane_t *controller = (decoder_pane_t *) self;
    controller->parent = args;
    for (int type_idx = -1; type_idx < decoder_orders_len; type_idx++) {
        DECODER type = type_idx == -1 ? DECODER_AUTO : decoder_orders[type_idx];
        pref_dropdown_string_entry_t *entry = &controller->vdec_entries[controller->vdec_entries_len];
        if (type == DECODER_AUTO) {
            entry->name = locstr("Automatic");
            entry->value = "auto";
            entry->fallback = true;
            controller->vdec_entries_len++;
            continue;
        }
        MODULE_DEFINITION def = decoder_definitions[type];
        if (!module_verify(&controller->parent->os_info, &def)) {
            continue;
        }
        entry->name = def.name;
        entry->value = def.id;
        entry->fallback = false;
        controller->vdec_entries_len++;
    }
    for (int type_idx = -1; type_idx < audio_orders_len; type_idx++) {
        AUDIO type = type_idx == -1 ? AUDIO_AUTO : audio_orders[type_idx];
        pref_dropdown_string_entry_t *entry = &controller->adec_entries[controller->adec_entries_len];
        if (type == AUDIO_AUTO) {
            entry->name = locstr("Automatic");
            entry->value = "auto";
            entry->fallback = true;
            controller->adec_entries_len++;
            continue;
        }
        MODULE_DEFINITION def = audio_definitions[type];
        if (!module_verify(&controller->parent->os_info, &def)) {
            continue;
        }
        entry->name = def.name;
        entry->value = def.id;
        entry->fallback = false;
        controller->adec_entries_len++;
    }
    int supported_ch = CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(module_audio_configuration());
    if (!supported_ch) {
        supported_ch = 2;
    }
    for (int i = 0; i < audio_config_len; i++) {
        audio_config_entry_t config = audio_configs[i];
        if (supported_ch < CHANNEL_COUNT_FROM_AUDIO_CONFIGURATION(config.configuration)) {
            continue;
        }
        struct pref_dropdown_int_entry_t *entry = &controller->surround_entries[controller->surround_entries_len];
        entry->name = locstr(config.name);
        entry->value = config.configuration;
        entry->fallback = config.configuration == AUDIO_CONFIGURATION_STEREO;
        controller->surround_entries_len++;
    }
}

static lv_obj_t *create_obj(lv_fragment_t *self, lv_obj_t *container) {
    decoder_pane_t *controller = (decoder_pane_t *) self;
    lv_obj_t *view = pref_pane_container(container);
    lv_obj_set_layout(view, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(view, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_t *decoder_label = pref_title_label(view, locstr("Video decoder"));
    lv_label_set_text_fmt(decoder_label, locstr("Video decoder - using %s"),
                          decoder_definitions[decoder_current].name);
    lv_obj_t *vdec_dropdown = pref_dropdown_string(view, controller->vdec_entries, controller->vdec_entries_len,
                                                   &app_configuration->decoder);
    lv_obj_set_width(vdec_dropdown, LV_PCT(100));
    lv_obj_t *audio_label = pref_title_label(view, locstr("Audio backend"));
    const char *audio_name = audio_current == AUDIO_DECODER ? locstr("Decoder provided")
                                                            : audio_definitions[audio_current].name;
    lv_label_set_text_fmt(audio_label, locstr("Audio backend - using %s"), audio_name);
    lv_obj_t *adec_dropdown = pref_dropdown_string(view, controller->adec_entries, controller->adec_entries_len,
                                                   &app_configuration->audio_backend);
    lv_obj_set_width(adec_dropdown, LV_PCT(100));

    lv_obj_t *hevc_checkbox = pref_checkbox(view, locstr("Prefer H265 codec"),
                                            &app_configuration->stream.supportsHevc,
                                            false);
    lv_obj_t *hevc_hint = pref_desc_label(view, NULL, false);
    if (!decoder_info.hevc) {
        lv_obj_add_state(hevc_checkbox, LV_STATE_DISABLED);
        lv_label_set_text_fmt(hevc_hint, locstr("%s decoder doesn't support H265 codec."),
                              decoder_definitions[decoder_current].name);
    } else {
        lv_obj_clear_state(hevc_checkbox, LV_STATE_DISABLED);
        lv_label_set_text(hevc_hint, locstr("H265 usually has clearer graphics, and is required "
                                            "for using HDR."));
    }

    lv_obj_t *hdr_checkbox = pref_checkbox(view, locstr("HDR (experimental)"), &app_configuration->stream.enableHdr,
                                           false);
    lv_obj_t *hdr_hint = pref_desc_label(view, NULL, false);
    if (decoder_info.hdr == DECODER_HDR_NONE) {
        lv_obj_add_state(hdr_checkbox, LV_STATE_DISABLED);
        lv_label_set_text_fmt(hdr_hint, locstr("%s decoder doesn't support HDR."),
                              decoder_definitions[decoder_current].name);
    } else if (!app_configuration->stream.supportsHevc) {
        lv_obj_clear_state(hdr_checkbox, LV_STATE_DISABLED);
        lv_label_set_text(hdr_hint, locstr("H265 is required to use HDR."));
    } else {
        lv_obj_clear_state(hdr_checkbox, LV_STATE_DISABLED);
        lv_label_set_text(hdr_hint, locstr("HDR is only supported on certain games and "
                                           "when connecting to supported monitor."));
    }
    lv_obj_t *hdr_more = pref_desc_label(view, locstr("Learn more about HDR feature."), true);
    lv_obj_set_style_text_color(hdr_more, lv_theme_get_color_primary(hdr_more), 0);
    lv_obj_add_flag(hdr_more, LV_OBJ_FLAG_CLICKABLE);

    pref_title_label(view, locstr("Sound Channels (Experimental)"));

    lv_obj_t *ch_dropdown = pref_dropdown_int(view, controller->surround_entries, controller->surround_entries_len,
                                              &app_configuration->stream.audioConfiguration);
    lv_obj_set_width(ch_dropdown, LV_PCT(100));

    lv_obj_add_event_cb(vdec_dropdown, pref_mark_restart_cb, LV_EVENT_VALUE_CHANGED, controller);
    lv_obj_add_event_cb(adec_dropdown, pref_mark_restart_cb, LV_EVENT_VALUE_CHANGED, controller);
    lv_obj_add_event_cb(hevc_checkbox, hdr_state_update_cb, LV_EVENT_VALUE_CHANGED, controller);
    lv_obj_add_event_cb(hdr_more, hdr_more_click_cb, LV_EVENT_CLICKED, NULL);

    controller->hdr_checkbox = hdr_checkbox;
    controller->hdr_hint = hdr_hint;

    return view;
}

static void pref_mark_restart_cb(lv_event_t *e) {
    decoder_pane_t *controller = (decoder_pane_t *) lv_event_get_user_data(e);
    settings_controller_t *parent = controller->parent;
    parent->needs_restart |= decoder_current != decoder_by_id(app_configuration->decoder);
    parent->needs_restart |= audio_current != audio_by_id(app_configuration->audio_backend);
}

static void hdr_state_update_cb(lv_event_t *e) {
    if (decoder_info.hdr == DECODER_HDR_NONE) return;
    decoder_pane_t *controller = (decoder_pane_t *) lv_event_get_user_data(e);
    if (!app_configuration->stream.supportsHevc) {
        lv_obj_add_state(controller->hdr_checkbox, LV_STATE_DISABLED);
        lv_label_set_text(controller->hdr_hint, locstr("H265 is required to use HDR."));
    } else {
        lv_obj_clear_state(controller->hdr_checkbox, LV_STATE_DISABLED);
        lv_label_set_text(controller->hdr_hint, locstr("HDR is only supported on certain games and "
                                                       "when connecting to supported monitor."));
    }
}

static void hdr_more_click_cb(lv_event_t *e) {
    app_open_url("https://github.com/mariotaku/moonlight-tv/wiki/HDR-Support");
}

