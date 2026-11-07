/*
 * Feed gvc-mixer-control.c with synthetic PulseAudio card and sink data.
 * PipeWire 1.6 keeps the sink index on a Bluetooth profile switch and
 * changes only the port. No sound server and no device is used.
 *
 * Build: see test-port-switch.sh.
 */
#include "gvc-mixer-control.c"

#define BT_CARD 374
#define BT_SINK 9
#define PCI_CARD 50
#define PCI_SINK 60

static guint last_active_output;
static guint n_warnings;

static void
on_active_output (GvcMixerControl *control, guint id, gpointer data)
{
        last_active_output = id;
}

static void
count_warnings (const gchar *domain, GLogLevelFlags level,
                const gchar *message, gpointer data)
{
        if (level & G_LOG_LEVEL_WARNING)
                n_warnings++;
        g_log_default_handler (domain, level, message, data);
}

static pa_card_profile_info2 *
profile2 (const char *name, guint n_sinks, guint n_sources)
{
        pa_card_profile_info2 *p = g_new0 (pa_card_profile_info2, 1);

        p->name = name;
        p->description = name;
        p->n_sinks = n_sinks;
        p->n_sources = n_sources;
        p->priority = 1;
        p->available = 1;
        return p;
}

static pa_card_port_info *
card_port (const char *name, const char *description, int direction,
           pa_card_profile_info2 *profile)
{
        pa_card_port_info *p = g_new0 (pa_card_port_info, 1);

        p->name = name;
        p->description = description;
        p->available = PA_PORT_AVAILABLE_YES;
        p->direction = direction;
        p->n_profiles = 1;
        p->profiles2 = g_new0 (pa_card_profile_info2 *, 2);
        p->profiles2[0] = profile;
        /* determine_profiles_for_port() reads the deprecated array. */
        p->profiles = (pa_card_profile_info **) p->profiles2;
        return p;
}

static void
add_card (GvcMixerControl *control, guint index, const char *name,
          pa_card_profile_info2 **profiles, guint n_profiles,
          pa_card_port_info **ports, guint n_ports)
{
        pa_card_info info = { 0 };
        pa_card_profile_info *old = g_new0 (pa_card_profile_info, n_profiles);
        guint i;

        for (i = 0; i < n_profiles; i++) {
                old[i].name = profiles[i]->name;
                old[i].description = profiles[i]->description;
                old[i].n_sinks = profiles[i]->n_sinks;
                old[i].n_sources = profiles[i]->n_sources;
                old[i].priority = profiles[i]->priority;
        }
        info.index = index;
        info.name = name;
        info.driver = "test";
        info.n_profiles = n_profiles;
        info.profiles = old;
        info.active_profile = &old[0];
        info.profiles2 = profiles;
        info.active_profile2 = profiles[0];
        info.proplist = pa_proplist_new ();
        pa_proplist_sets (info.proplist, "device.description", name);
        info.n_ports = n_ports;
        info.ports = ports;
        update_card (control, &info);
        pa_proplist_free (info.proplist);
}

/* Send one sink update with the given port list and active port. */
static void
send_sink (GvcMixerControl *control, guint index, guint card,
           const char *name, gboolean bluetooth,
           const char **port_names, guint n_ports, const char *active)
{
        pa_sink_info info = { 0 };
        pa_sink_port_info **ports = g_new0 (pa_sink_port_info *, n_ports + 1);
        guint i;

        for (i = 0; i < n_ports; i++) {
                ports[i] = g_new0 (pa_sink_port_info, 1);
                ports[i]->name = port_names[i];
                ports[i]->description = port_names[i];
                ports[i]->available = PA_PORT_AVAILABLE_YES;
                if (g_strcmp0 (port_names[i], active) == 0)
                        info.active_port = ports[i];
        }
        info.name = name;
        info.index = index;
        info.description = name;
        info.card = card;
        info.n_ports = n_ports;
        info.ports = ports;
        pa_channel_map_init_stereo (&info.channel_map);
        pa_cvolume_set (&info.volume, 2, PA_VOLUME_NORM);
        info.base_volume = PA_VOLUME_NORM;
        info.proplist = pa_proplist_new ();
        if (bluetooth)
                pa_proplist_sets (info.proplist, "device.bus", "bluetooth");
        update_sink (control, &info);
        pa_proplist_free (info.proplist);
        for (i = 0; i < n_ports; i++)
                g_free (ports[i]);
        g_free (ports);
}

static void
send_source (GvcMixerControl *control, guint index, guint card,
             const char *name, const char **port_names, guint n_ports,
             const char *active)
{
        pa_source_info info = { 0 };
        pa_source_port_info **ports = g_new0 (pa_source_port_info *, n_ports + 1);
        guint i;

        for (i = 0; i < n_ports; i++) {
                ports[i] = g_new0 (pa_source_port_info, 1);
                ports[i]->name = port_names[i];
                ports[i]->description = port_names[i];
                ports[i]->available = PA_PORT_AVAILABLE_YES;
                if (g_strcmp0 (port_names[i], active) == 0)
                        info.active_port = ports[i];
        }
        info.name = name;
        info.index = index;
        info.description = name;
        info.card = card;
        info.monitor_of_sink = PA_INVALID_INDEX;
        info.n_ports = n_ports;
        info.ports = ports;
        pa_channel_map_init_mono (&info.channel_map);
        pa_cvolume_set (&info.volume, 1, PA_VOLUME_NORM);
        info.base_volume = PA_VOLUME_NORM;
        info.proplist = pa_proplist_new ();
        pa_proplist_sets (info.proplist, "device.bus", "bluetooth");
        update_source (control, &info);
        pa_proplist_free (info.proplist);
        for (i = 0; i < n_ports; i++)
                g_free (ports[i]);
        g_free (ports);
}

static GvcMixerUIDevice *
output_for_port (GvcMixerControl *control, const char *port)
{
        GList *devices = g_hash_table_get_values (control->priv->ui_outputs);
        GvcMixerUIDevice *found = NULL;
        GList *d;

        for (d = devices; d != NULL; d = d->next) {
                if (g_strcmp0 (gvc_mixer_ui_device_get_port (d->data), port) == 0)
                        found = d->data;
        }
        g_list_free (devices);
        g_assert_nonnull (found);
        return found;
}

static GvcMixerStream *
sink_for_index (GvcMixerControl *control, guint index)
{
        GvcMixerStream *sink;

        sink = g_hash_table_lookup (control->priv->sinks, GUINT_TO_POINTER (index));
        g_assert_nonnull (sink);
        return sink;
}

static GvcMixerControl *
new_control (void)
{
        GvcMixerControl *control = gvc_mixer_control_new ("test");

        g_signal_connect (control, "active-output-update",
                          G_CALLBACK (on_active_output), NULL);
        last_active_output = 0;
        n_warnings = 0;
        return control;
}

static void
assert_active (GvcMixerControl *control, GvcMixerStream *sink,
               GvcMixerUIDevice *expected)
{
        GvcMixerUIDevice *found;

        found = gvc_mixer_control_lookup_device_from_stream (control, sink);
        g_assert_true (found == expected);
        g_assert_cmpuint (last_active_output, ==,
                          gvc_mixer_ui_device_get_id (expected));
}

static GvcMixerControl *
bt_setup (void)
{
        GvcMixerControl *control = new_control ();
        pa_card_profile_info2 *profiles[] = {
                profile2 ("a2dp-sink", 1, 0),
                profile2 ("headset-head-unit", 1, 1),
        };
        pa_card_port_info *ports[] = {
                card_port ("headset-output", "Headphones",
                           PA_DIRECTION_OUTPUT, profiles[0]),
                card_port ("headset-hf-output", "Handsfree",
                           PA_DIRECTION_OUTPUT, profiles[1]),
                card_port ("headset-input", "Handsfree",
                           PA_DIRECTION_INPUT, profiles[1]),
        };

        add_card (control, BT_CARD, "bluez_card.test", profiles, 2, ports, 3);
        return control;
}

static void
test_bt_port_switch (void)
{
        GvcMixerControl *control = bt_setup ();
        const char *a2dp[] = { "headset-output" };
        const char *hfp[] = { "headset-hf-output" };
        GvcMixerUIDevice *headphones, *handsfree;
        GvcMixerStream *sink;

        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   a2dp, 1, "headset-output");
        update_default_sink_from_name (control, "bluez_output.test");
        sink = sink_for_index (control, BT_SINK);
        headphones = output_for_port (control, "headset-output");
        handsfree = output_for_port (control, "headset-hf-output");
        assert_active (control, sink, headphones);

        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   hfp, 1, "headset-hf-output");
        assert_active (control, sink, handsfree);
        g_assert_cmpint (gvc_mixer_ui_device_get_stream_id (headphones), ==,
                         GVC_MIXER_UI_DEVICE_INVALID);

        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   a2dp, 1, "headset-output");
        assert_active (control, sink, headphones);
        g_assert_cmpint (gvc_mixer_ui_device_get_stream_id (handsfree), ==,
                         GVC_MIXER_UI_DEVICE_INVALID);
        g_test_message ("warnings: %u", n_warnings);
}

static void
test_bt_ports_arrive_late (void)
{
        GvcMixerControl *control = bt_setup ();
        const char *a2dp[] = { "headset-output" };
        GvcMixerStream *sink;

        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   NULL, 0, NULL);
        update_default_sink_from_name (control, "bluez_output.test");
        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   a2dp, 1, "headset-output");
        sink = sink_for_index (control, BT_SINK);
        assert_active (control, sink, output_for_port (control, "headset-output"));
        g_test_message ("warnings: %u", n_warnings);
}

static void
test_pci_port_switch (void)
{
        GvcMixerControl *control = new_control ();
        pa_card_profile_info2 *profiles[] = {
                profile2 ("output:analog-stereo", 1, 0),
        };
        pa_card_port_info *ports[] = {
                card_port ("analog-output-speaker", "Speakers",
                           PA_DIRECTION_OUTPUT, profiles[0]),
                card_port ("analog-output-headphones", "Headphones",
                           PA_DIRECTION_OUTPUT, profiles[0]),
        };
        const char *both[] = { "analog-output-speaker", "analog-output-headphones" };
        GvcMixerUIDevice *speakers, *headphones;
        GvcMixerStream *sink;

        add_card (control, PCI_CARD, "alsa_card.test", profiles, 1, ports, 2);
        send_sink (control, PCI_SINK, PCI_CARD, "alsa_output.test", FALSE,
                   both, 2, "analog-output-speaker");
        update_default_sink_from_name (control, "alsa_output.test");
        sink = sink_for_index (control, PCI_SINK);
        speakers = output_for_port (control, "analog-output-speaker");
        headphones = output_for_port (control, "analog-output-headphones");
        assert_active (control, sink, speakers);

        send_sink (control, PCI_SINK, PCI_CARD, "alsa_output.test", FALSE,
                   both, 2, "analog-output-headphones");
        assert_active (control, sink, headphones);
        /* Both ports stay on the same sink, so both devices keep it. */
        g_assert_cmpint (gvc_mixer_ui_device_get_stream_id (speakers), ==,
                         (gint) gvc_mixer_stream_get_id (sink));
        g_assert_cmpint (gvc_mixer_ui_device_get_stream_id (headphones), ==,
                         (gint) gvc_mixer_stream_get_id (sink));
        g_test_message ("warnings: %u", n_warnings);
}

static void
test_bt_ports_lost (void)
{
        GvcMixerControl *control = bt_setup ();
        const char *a2dp[] = { "headset-output" };
        GvcMixerUIDevice *headphones;
        GvcMixerStream *sink;

        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   a2dp, 1, "headset-output");
        update_default_sink_from_name (control, "bluez_output.test");
        sink = sink_for_index (control, BT_SINK);
        headphones = output_for_port (control, "headset-output");
        assert_active (control, sink, headphones);

        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   NULL, 0, NULL);
        g_assert_cmpint (gvc_mixer_ui_device_get_stream_id (headphones), ==,
                         GVC_MIXER_UI_DEVICE_INVALID);
        g_assert_null (gvc_mixer_control_lookup_device_from_stream (control, sink));

        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   a2dp, 1, "headset-output");
        assert_active (control, sink, headphones);
}

static void
test_network_sink (void)
{
        GvcMixerControl *control = new_control ();

        send_sink (control, 70, PA_INVALID_INDEX, "tunnel.test", FALSE,
                   NULL, 0, NULL);
        send_sink (control, 70, PA_INVALID_INDEX, "tunnel.test", FALSE,
                   NULL, 0, NULL);
        g_assert_cmpuint (g_hash_table_size (control->priv->ui_outputs), ==, 1);
        update_default_sink_from_name (control, "tunnel.test");
        assert_active (control, sink_for_index (control, 70),
                       g_hash_table_get_values (control->priv->ui_outputs)->data);
}

static void
test_network_and_bt (void)
{
        GvcMixerControl *control = bt_setup ();
        const char *a2dp[] = { "headset-output" };
        const char *hfp[] = { "headset-hf-output" };

        send_sink (control, 70, PA_INVALID_INDEX, "tunnel.test", FALSE,
                   NULL, 0, NULL);
        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   a2dp, 1, "headset-output");
        update_default_sink_from_name (control, "bluez_output.test");
        send_sink (control, BT_SINK, BT_CARD, "bluez_output.test", TRUE,
                   hfp, 1, "headset-hf-output");
        assert_active (control, sink_for_index (control, BT_SINK),
                       output_for_port (control, "headset-hf-output"));
}

static void
test_bt_source_late_ports (void)
{
        GvcMixerControl *control = bt_setup ();
        const char *mic[] = { "headset-input" };
        GvcMixerStream *source;
        GvcMixerUIDevice *input = NULL;
        GList *devices, *d;

        send_source (control, 12, BT_CARD, "bluez_input.test", NULL, 0, NULL);
        update_default_source_from_name (control, "bluez_input.test");
        send_source (control, 12, BT_CARD, "bluez_input.test", mic, 1, "headset-input");
        source = g_hash_table_lookup (control->priv->sources, GUINT_TO_POINTER (12));
        g_assert_nonnull (source);
        devices = g_hash_table_get_values (control->priv->ui_inputs);
        for (d = devices; d != NULL; d = d->next)
                if (g_strcmp0 (gvc_mixer_ui_device_get_port (d->data), "headset-input") == 0)
                        input = d->data;
        g_list_free (devices);
        g_assert_nonnull (input);
        g_assert_true (gvc_mixer_control_lookup_device_from_stream (control, source) == input);
}

static void
run_in_subprocess (gconstpointer data)
{
        const char *path = data;

        g_test_trap_subprocess (g_strconcat (path, "/subprocess", NULL), 0,
                                G_TEST_SUBPROCESS_INHERIT_STDOUT |
                                G_TEST_SUBPROCESS_INHERIT_STDERR);
        g_test_trap_assert_passed ();
}

int
main (int argc, char **argv)
{
        g_test_init (&argc, &argv, NULL);
        g_test_set_nonfatal_assertions ();
        g_log_set_always_fatal (G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);
        g_log_set_handler ("sound-cc-panel", G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE,
                           count_warnings, NULL);

        g_test_add_func ("/port-switch/bt/subprocess", test_bt_port_switch);
        g_test_add_func ("/port-switch/bt-late-ports/subprocess", test_bt_ports_arrive_late);
        g_test_add_func ("/port-switch/pci/subprocess", test_pci_port_switch);
        g_test_add_func ("/port-switch/bt-ports-lost/subprocess", test_bt_ports_lost);
        g_test_add_func ("/port-switch/network/subprocess", test_network_sink);
        g_test_add_func ("/port-switch/network-and-bt/subprocess", test_network_and_bt);
        g_test_add_func ("/port-switch/bt-source/subprocess", test_bt_source_late_ports);
        g_test_add_data_func ("/port-switch/bt", "/port-switch/bt", run_in_subprocess);
        g_test_add_data_func ("/port-switch/bt-late-ports", "/port-switch/bt-late-ports",
                              run_in_subprocess);
        g_test_add_data_func ("/port-switch/pci", "/port-switch/pci", run_in_subprocess);
        g_test_add_data_func ("/port-switch/bt-ports-lost", "/port-switch/bt-ports-lost",
                              run_in_subprocess);
        g_test_add_data_func ("/port-switch/network", "/port-switch/network",
                              run_in_subprocess);
        g_test_add_data_func ("/port-switch/network-and-bt", "/port-switch/network-and-bt",
                              run_in_subprocess);
        g_test_add_data_func ("/port-switch/bt-source", "/port-switch/bt-source",
                              run_in_subprocess);

        return g_test_run ();
}
