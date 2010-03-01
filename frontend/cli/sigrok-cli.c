/*
 *   sigrok - sigrok-cli.c
 *
 *   Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <string.h>
#include <poll.h>
#include <time.h>
#include <glib.h>

#include "config.h"
#include "sigrok.h"
#include "backend.h"
#include "hwplugin.h"
#include "device.h"
#include "session.h"

#include <libusb.h>
#include <sys/time.h>

#define SIGROK_CLI_VERSION "0.1"

extern struct hwcap_option hwcap_options[];

gboolean debug = FALSE;
GMainContext *gmaincontext = NULL;
GMainLoop *gmainloop = NULL;
struct termios term_orig = {0};
int format_bpl = 64;
char format_base = 'b';
int limit_samples = 0;

static gboolean opt_version = FALSE;
static gboolean opt_list_hwplugins = FALSE;
static gboolean opt_list_devices = FALSE;
static gboolean opt_list_analyzers = FALSE;
static gchar *load_session_filename = NULL;
static gchar *save_session_filename = NULL;
static int opt_device = -1;
static gchar *opt_probes = NULL;
static gchar **opt_devoption = NULL;
static gchar *opt_analyzers = NULL;
static gchar *opt_format = NULL;
static gchar *opt_seconds = NULL;
static gchar *opt_samples = NULL;
static GOptionEntry optargs[] =
{
	{ "version", 'V', 0, G_OPTION_ARG_NONE, &opt_version, "Show version", NULL },
	{ "list-hardware-plugins", 'H', 0, G_OPTION_ARG_NONE, &opt_list_hwplugins, "List hardware plugins", NULL },
	{ "list-devices", 'D', 0, G_OPTION_ARG_NONE, &opt_list_devices, "List devices", NULL },
	{ "list-analyzer-plugins", 'A', 0, G_OPTION_ARG_NONE, &opt_list_analyzers, "List analyzer plugins", NULL },

	{ "load-session-file", 'L', 0, G_OPTION_ARG_FILENAME, &load_session_filename, "Load session from file", NULL },
	{ "save-session-file", 'S', 0, G_OPTION_ARG_FILENAME, &save_session_filename, "Save session to file", NULL },
	{ "device", 'd', 0, G_OPTION_ARG_INT, &opt_device, "Use device id", NULL },
	{ "probes", 'p', 0, G_OPTION_ARG_STRING, &opt_probes, "Probes to use", NULL },
	{ "device-option", 'o', 0, G_OPTION_ARG_STRING_ARRAY, &opt_devoption, "Device-specific option", NULL },
	{ "analyzers", 'a', 0, G_OPTION_ARG_STRING, &opt_analyzers, "Protocol analyzer sequence", NULL },
	{ "format", 'f', 0, G_OPTION_ARG_STRING, &opt_format, "Output format", NULL },

	{ "seconds", 0, 0, G_OPTION_ARG_STRING, &opt_seconds, "Number of seconds to sample", NULL },
	{ "samples", 0, 0, G_OPTION_ARG_STRING, &opt_samples, "Number of samples to acquire", NULL },

	{ NULL }

};

void show_version(void)
{

	printf("Sigrok version %s\nCLI version %s\n", VERSION, SIGROK_CLI_VERSION);

}


void show_hwplugin_list(void)
{
	GSList *plugins, *p;
	struct device_plugin *plugin;

	printf("Plugins for the following devices are installed:\n");
	plugins = list_hwplugins();
	for(p = plugins; p; p = p->next)
	{
		plugin = p->data;
		printf(" %s\n", plugin->name);
	}

}


void show_device_list()
{
	struct device *device;
	struct hwcap_option *hwo;
	GSList *devices, *l;
	int devcnt, *capabilities, cap, i;
	float *sample_rates;
	char *idstring, *title;

	devcnt = 0;
	device_scan();
	devices = device_list();
	if(g_slist_length(devices) > 0)
	{
		printf("The following devices were found:\nID  Device\n");
		for(l = devices; l; l = l->next)
		{
			device = l->data;

			/* device and probes */
			idstring = device->plugin->get_device_info(device->plugin_index, DI_IDENTIFIER);
			printf("%-3d %s %s", devcnt, device->plugin->name, idstring);
			g_free(idstring);
			if(device->probes)
				printf(" with %d probes", g_slist_length(device->probes));
			printf("\n");

			/* supported options */
			title = "    Supported options:\n";
			capabilities = device->plugin->get_capabilities();
			for(cap = 0; capabilities[cap]; cap++)
			{
				hwo = find_hwcap_option(capabilities[cap]);
				if(hwo)
				{
					if(title)
					{
						printf("%s", title);
						title = NULL;
					}
					if(hwo->capability == HWCAP_SAMPLERATE)
					{
						printf("      %s", hwo->shortname);
						/* supported sample rates */
						sample_rates = (float *) device->plugin->get_device_info(device->plugin_index, DI_SAMPLE_RATES);
						if(sample_rates)
						{
							printf(" - supported sample rates:\n");
							for(i = 0; sample_rates[i]; i++)
								printf("        %7.3f\n", sample_rates[i]);
						}
						else
							printf("\n");
					}
					else
						printf("      %s\n", hwo->shortname);
				}
			}
		}
	}

}


void show_analyzer_list()
{

	/* TODO: implement */

}


void flush_linebufs(GSList *probes, char *linebuf, int linebuf_len)
{
	static int num_probes = 0;
	static int max_probename_len = 0;

	struct probe *probe;
	int len, p;

	if(linebuf[0])
	{
		if(num_probes == 0)
		{
			/* first time through */
			num_probes = g_slist_length(probes);
			for(p = 0; p < num_probes; p++)
			{
				probe = g_slist_nth_data(probes, p);
				if(probe->enabled)
				{
					len = strlen(probe->name);
					if(len > max_probename_len)
						max_probename_len = len;
				}
			}
		}

		for(p = 0; p < num_probes; p++)
		{
			probe = g_slist_nth_data(probes, p);
			if(probe->enabled)
				printf("%*s:%s\n", max_probename_len, probe->name, linebuf + p * linebuf_len);
		}
		memset(linebuf, 0, num_probes * linebuf_len);
	}

}


void datafeed_callback(struct device *device, struct datafeed_packet *packet)
{
	static int num_probes = 0;
	static int received_samples = 0;
	static int linebuf_len = 0;
	static char *linebuf = NULL;

	struct probe *probe;
	struct datafeed_header *header;
	int num_enabled_probes, offset, p, buf_offset, bpl_cnt, i;
	unsigned int sample;

	/* if the first packet to come in isn't a header, don't even try */
	if(packet->type != DF_HEADER && linebuf == NULL)
		return;

	if(packet->type == DF_HEADER)
	{
		header = (struct datafeed_header *) packet->payload;
		num_enabled_probes = 0;
		for(i = 0; i < header->num_probes; i++)
		{
			probe = g_slist_nth_data(device->probes, i);
			if(probe->enabled)
				num_enabled_probes++;
		}
		printf("Acquisition with %d/%d probes at %.3f Mhz starting at %s",
				num_enabled_probes, header->num_probes, header->rate,
				ctime(&header->starttime.tv_sec));
		num_probes = header->num_probes;

		linebuf_len = format_bpl * 2;
		linebuf = g_malloc0(num_probes * linebuf_len);
	}
	else if(packet->type == DF_END)
	{
		flush_linebufs(device->probes, linebuf, linebuf_len);
		g_main_loop_quit(gmainloop);
	}
	else if(packet->type == DF_LOGIC8)
	{
		/* every byte represents a complete sample set, with num_probes being the
		 * number of bits that are relevant. we're handling all probes here, not
		 * checking whether they're enabled or not. flush_linebufs() will skip them.
		 */
		if(format_base == 'b')
		{
			buf_offset = bpl_cnt = 0;
			for(offset = 0; received_samples < limit_samples && offset < packet->length; offset++)
			{
				sample = packet->payload[offset];
				for(p = 0; p < num_probes; p++)
				{
					if(sample & (1 << p))
						linebuf[p * linebuf_len + buf_offset] = '1';
					else
						linebuf[p * linebuf_len + buf_offset] = '0';
				}
				buf_offset++;
				bpl_cnt++;

				/* space every 8th bit */
				if((bpl_cnt & 7) == 0)
				{
					for(p = 0; p < num_probes; p++)
						linebuf[p * linebuf_len + buf_offset] = ' ';
					buf_offset++;
				}

				/* end of line */
				if(bpl_cnt >= format_bpl)
				{
					flush_linebufs(device->probes, linebuf, linebuf_len);
					buf_offset = bpl_cnt = 0;
				}
				received_samples++;
			}
		}
		else
		{
			/* TODO: implement */
		}
	}

}


gboolean stdin_prepare(GSource *source, int *timeout)
{

	*timeout = -1;

	return FALSE;
}


gboolean stdin_check(GSource *source)
{
	struct pollfd pfd;

	pfd.fd = STDIN_FILENO;
	pfd.events = POLLIN;
	if(poll(&pfd, 1, 0))
		return TRUE;

	return FALSE;
}


gboolean stdin_dispatch(GSource *source, GSourceFunc callback, gpointer data)
{

	g_main_loop_quit(gmainloop);

	return TRUE;
}


GSourceFuncs stdin_funcs = {
	stdin_prepare,
	stdin_check,
	stdin_dispatch,
	NULL
};


char **parse_probestring(int max_probes, char *probestring)
{
	int tmp, b, e, i;
	char **tokens, **range, **probelist, *name, str[8];
	gboolean error;

	error = FALSE;
	range = NULL;
	probelist = g_malloc0(max_probes * sizeof(char *));
	tokens = g_strsplit(probestring, ",", max_probes);
	for(i = 0; tokens[i]; i++)
	{
		if(strchr(tokens[i], '-'))
		{
			/* a range of probes in the form 1-5 */
			range = g_strsplit(tokens[i], "-", 2);
			if(!range[0] || !range[1] || range[2])
			{
				/* need exactly two arguments */
				printf("Invalid probe syntax '%s'.\n", tokens[i]);
				error = TRUE;
				break;
			}

			b = atoi(range[0]);
			e = atoi(range[1]);
			if(b < 1 || e > max_probes || b >= e)
			{
				printf("Invalid probe range '%s'.\n", tokens[i]);
				error = TRUE;
				break;
			}

			while(b <= e)
			{
				snprintf(str, 7, "%d", b);
				probelist[b-1] = g_strdup(str);
				b++;
			}
		}
		else
		{
			tmp = atoi(tokens[i]);
			if(tmp < 1 || tmp > max_probes)
			{
				printf("Invalid probe %d.\n", tmp);
				error = TRUE;
				break;
			}

			if( (name=strchr(tokens[i], '=')) )
				probelist[tmp-1] = g_strdup(++name);
			else
			{
				snprintf(str, 7, "%d", tmp);
				probelist[tmp-1] = g_strdup(str);
			}
		}
	}

	if(error)
	{
		for(i = 0; i < max_probes; i++)
			if(probelist[i])
				g_free(probelist[i]);
		g_free(probelist);
		probelist = NULL;
	}

	g_strfreev(tokens);
	if(range)
		g_strfreev(range);

	if(debug && probelist)
		for(i = 0; i < max_probes; i++)
		{
			if(probelist[i])
				g_message("Probe %d: '%s'", i+1, probelist[i]);
		}

	return probelist;
}


void run_session(void)
{
	struct device *device;
	struct probe *probe;
	struct termios term;
	GSource *stdin_source;
	GPollFD stdin_pfd;
	GSList *devices;
	int num_devices, max_probes, ret, i, j;
	char **probelist, *val;

	device_scan();
	devices = device_list();
	num_devices = g_slist_length(devices);
	if(num_devices == 0)
	{
		g_warning("No devices found.");
		return;
	}
	if(opt_device == -1)
	{
		if(num_devices == 1)
			/* no device specified, but there is only one */
			opt_device = 0;
		else
		{
			g_warning("%d devices found, please select one.", num_devices);
			return;
		}
	}
	else
	{
		if(opt_device >= num_devices)
		{
			g_warning("Device not found.");
			return;
		}
	}

	session_new();
	session_output_add_callback(datafeed_callback);

	device = g_slist_nth_data(devices, opt_device);
	if(session_device_add(device) != SIGROK_OK)
	{
		printf("Failed to use device.\n");
		session_destroy();
		return;
	}

	if(opt_probes)
	{
		/* this only works because a device by default initializes and enables all its probes */
		max_probes = g_slist_length(device->probes);
		probelist = parse_probestring(max_probes, opt_probes);
		if(!probelist)
		{
			session_destroy();
			return;
		}

		for(i = 0; i < max_probes; i++)
		{
			if(probelist[i])
			{
				device_probe_name(device, i+1, probelist[i]);
				g_free(probelist[i]);
			}
			else
			{
				probe = probe_find(device, i+1);
				probe->enabled = FALSE;
			}
		}
		g_free(probelist);
	}

	if(opt_seconds)
	{
		device->plugin->set_configuration(device->plugin_index, HWCAP_LIMIT_SECONDS, opt_seconds);
	}
	if(opt_samples)
	{
		limit_samples = atoi(opt_samples);
		device->plugin->set_configuration(device->plugin_index, HWCAP_LIMIT_SAMPLES, opt_samples);
	}

	if(opt_devoption)
	{
		for(i = 0; opt_devoption[i]; i++)
		{
			if( (val = strchr(opt_devoption[i], '=')) )
			{
				*val = 0;
				for(j = 0; hwcap_options[j].capability; j++)
				{
					if(!strcmp(hwcap_options[i].shortname, opt_devoption[i]))
					{
						ret = device->plugin->set_configuration(device->plugin_index, hwcap_options[j].capability, ++val);
						if(ret != SIGROK_OK)
						{
							printf("Failed to set device option '%s'.\n", opt_devoption[i]);
							session_destroy();
							return;
						}
					}
				}
			}
			else
			{
				printf("Invalid device option '%s'.\n", opt_devoption[i]);
				session_destroy();
				return;
			}
		}
	}

	if(session_start() != SIGROK_OK)
	{
		printf("Failed to start session.\n");
		session_destroy();
		return;
	}

	gmaincontext = g_main_context_default();

	if(!opt_seconds && !opt_samples)
	{
		/* monitor stdin along with the device's I/O */
		tcgetattr(STDIN_FILENO, &term);
		memcpy(&term_orig, &term, sizeof(struct termios));
		term.c_lflag &= ~(ECHO | ICANON | ISIG);
		tcsetattr(STDIN_FILENO, TCSADRAIN, &term);

		stdin_source = g_source_new(&stdin_funcs, sizeof(GSource));
		stdin_pfd.fd = STDIN_FILENO;
		stdin_pfd.events =  G_IO_IN | G_IO_HUP | G_IO_ERR;
		g_main_context_add_poll(gmaincontext, &stdin_pfd, G_PRIORITY_DEFAULT);
		g_source_add_poll(stdin_source, &stdin_pfd);
		ret = g_source_attach(stdin_source, gmaincontext);

		printf("press any key to stop\n");
	}

	gmainloop = g_main_loop_new(gmaincontext, FALSE);
	g_main_loop_run(gmainloop);

	if(!opt_seconds && !opt_samples)
	{
		tcflush(STDIN_FILENO, TCIFLUSH);
		tcsetattr(STDIN_FILENO, TCSANOW, &term_orig);
	}
	session_stop();

	session_destroy();

}


void logger(const gchar *log_domain, GLogLevelFlags log_level, const gchar *message, gpointer user_data)
{

	if(log_level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_WARNING))
	{
		fprintf(stderr, "Warning: %s\n", message);
		fflush(stderr);
	}
	else
	{
		if(debug)
		{
			printf("* %s\n", message);
			fflush(stdout);
		}
	}

}


int main(int argc, char **argv)
{
	GOptionContext *context;
	GError *error;
	int val, i;

	printf("Sigrok version %s\n", PACKAGE_VERSION);
	g_log_set_default_handler(logger, NULL);
	if(getenv("SIGROK_DEBUG"))
		debug = TRUE;

	error = NULL;
	context = g_option_context_new(NULL);
	g_option_context_add_main_entries(context, optargs, NULL);
	if(!g_option_context_parse(context, &argc, &argv, &error))
	{
		g_warning("%s", error->message);
		return 1;
	}

	if(sigrok_init() != SIGROK_OK)
		return 1;

	if(opt_format)
	{
		val = atoi(opt_format);
		if(val > 0)
			format_bpl = val;
		for(i = 0; opt_format[i]; i++)
			if(opt_format[i] == 'b' || opt_format[i] == 'h')
			{
				format_base = opt_format[i];
				break;
			}
	}

	if(opt_version)
		show_version();
	else if(opt_list_hwplugins)
		show_hwplugin_list();
	else if(opt_list_devices)
		show_device_list();
	else if(opt_list_analyzers)
		show_analyzer_list();
	else if(opt_samples || opt_seconds)
		run_session();
	else
		printf("%s", g_option_context_get_help(context, TRUE, NULL));

	sigrok_cleanup();

	return 0;
}


