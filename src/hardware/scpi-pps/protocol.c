/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include "scpi.h"
#include "protocol.h"

SR_PRIV int select_channel(const struct sr_dev_inst *sdi, struct sr_channel *ch)
{
	struct dev_context *devc;
	struct pps_channel *cur_pch, *new_pch;
	int ret;

	if (g_slist_length(sdi->channels) == 1)
		return SR_OK;

	devc = sdi->priv;
	if (ch == devc->cur_channel)
		return SR_OK;

	new_pch = ch->priv;
	if (devc->cur_channel) {
		cur_pch = devc->cur_channel->priv;
		if (cur_pch->hw_output_idx == new_pch->hw_output_idx) {
			/* Same underlying output channel. */
			devc->cur_channel = ch;
			return SR_OK;
		}
	}

	if ((ret = scpi_cmd(sdi, devc->device->commands, SCPI_CMD_SELECT_CHANNEL,
			new_pch->hwname)) >= 0)
		devc->cur_channel = ch;

	return ret;
}

SR_PRIV struct sr_channel *next_enabled_channel(const struct sr_dev_inst *sdi,
		struct sr_channel *cur_channel)
{
	struct sr_channel *next_channel;
	GSList *l;

	next_channel = cur_channel;
	do {
		l = g_slist_find(sdi->channels, next_channel);
		if (l && l->next)
			next_channel = l->next->data;
		else
			next_channel = sdi->channels->data;
	} while (!next_channel->enabled);

	return next_channel;
}

SR_PRIV int scpi_pps_receive_data(int fd, int revents, void *cb_data)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	const struct sr_dev_inst *sdi;
	struct sr_channel *next_channel;
	struct sr_scpi_dev_inst *scpi;
	struct pps_channel *pch;
	float f;
	int cmd;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	scpi = sdi->conn;

	/* Retrieve requested value for this state. */
	if (sr_scpi_get_float(scpi, NULL, &f) == SR_OK) {
		pch = devc->cur_channel->priv;
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		analog.channels = g_slist_append(NULL, devc->cur_channel);
		analog.num_samples = 1;
		analog.mq = pch->mq;
		if (pch->mq == SR_MQ_VOLTAGE)
			analog.unit = SR_UNIT_VOLT;
		else if (pch->mq == SR_MQ_CURRENT)
			analog.unit = SR_UNIT_AMPERE;
		else if (pch->mq == SR_MQ_POWER)
			analog.unit = SR_UNIT_WATT;
		analog.mqflags = SR_MQFLAG_DC;
		analog.data = &f;
		sr_session_send(sdi, &packet);
		g_slist_free(analog.channels);
	}

	if (g_slist_length(sdi->channels) > 1) {
		next_channel = next_enabled_channel(sdi, devc->cur_channel);
		if (select_channel(sdi, next_channel) != SR_OK) {
			sr_err("Failed to select channel %s", next_channel->name);
			return FALSE;
		}
	}

	pch = devc->cur_channel->priv;
	if (pch->mq == SR_MQ_VOLTAGE)
		cmd = SCPI_CMD_GET_MEAS_VOLTAGE;
	else if (pch->mq == SR_MQ_FREQUENCY)
		cmd = SCPI_CMD_GET_MEAS_FREQUENCY;
	else if (pch->mq == SR_MQ_CURRENT)
		cmd = SCPI_CMD_GET_MEAS_CURRENT;
	else if (pch->mq == SR_MQ_POWER)
		cmd = SCPI_CMD_GET_MEAS_POWER;
	else
		return SR_ERR;
	scpi_cmd(sdi, devc->device->commands, cmd);

	return TRUE;
}
