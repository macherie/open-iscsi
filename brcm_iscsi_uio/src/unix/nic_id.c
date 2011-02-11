/* nic_id.c: Using sysfs to determine the PCI vendor, device, subvendor and
 *           subdevice ID's
 *
 * Copyright (c) 2004-2010 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.
 *
 * Written by: Benjamin Li  (benli@broadcom.com)
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#include <linux/if.h>

#include "logger.h"
#include "nic.h"

#define PFX "nic_id "

/*******************************************************************************
 * Sysfs constant strings used to get PCI vendor, and device ID's
 ******************************************************************************/
const char uio_vendor_id_template[] = "/sys/class/uio/uio%d/device/vendor";
const char uio_subvendor_id_template[] = "/sys/class/uio/uio%d/device/subsystem_vendor";
const char uio_device_id_template[] = "/sys/class/uio/uio%d/device/device";
const char uio_subdevice_id_template[] = "/sys/class/uio/uio%d/device/subsystem_device";
const char uio_device_symlink_template[] = "/sys/class/uio/uio%d/device";

/**
 *  get_id() - Utility function to read hex values from sysfs
 *  @param nic - NIC device to use
 *  @param sysfs_template - sysfs path template to use
 *  @param sysfs_template_size - sysfs path template size in bytes
 *  @parm id - this is the value returned from the sysfs entry
 *  @return 0 on success <0 on failure
 */
static int get_id(nic_t *nic,
		  const char *sysfs_template,
		  const size_t sysfs_template_size,
		  uint32_t *id)
{
	int rc = 0;
	FILE *fp;
	size_t chars_read;
	char buf[7];
	char *path;
	size_t path_size;

	path_size = sysfs_template_size + 4;
	path = malloc(path_size);
	if(path == NULL)
	{
		LOG_ERR("Could not allocate memory for %s", sysfs_template);
		return -ENOMEM;
	}

	snprintf(path, path_size, sysfs_template, nic->uio_minor);

	fp = fopen(path, "r");
	if(fp == NULL)
	{
		LOG_ERR("Could not open path: %s", path);
		rc = -EIO;
		goto error_fopen;
	}

	chars_read = fread(buf, sizeof(buf), 1, fp); 
	if(chars_read != 1)
	{
		LOG_ERR("Could not read from: %s", path);
		rc = -EIO;
		goto error;
	}

	chars_read = sscanf(buf, "%x", id);
	if(chars_read != 1)
	{
		LOG_ERR("Could interpret value: %s from: %s",
			buf, path);
		rc = -EIO;
		goto error;
	}

error:
	fclose(fp);

error_fopen:
	free(path);

	return rc;
}

static int get_vendor(nic_t *nic,
		      uint32_t *id)
{
	return get_id(nic,
		      uio_vendor_id_template, sizeof(uio_vendor_id_template),
		      id);
}

static int get_subvendor(nic_t *nic,
			 uint32_t *id)
{
	return get_id(nic,
		      uio_subvendor_id_template,
		      sizeof(uio_subvendor_id_template),
		      id);
}

static int get_device(nic_t *nic,
		      uint32_t *id)
{
	return get_id(nic,
		      uio_device_id_template,
		      sizeof(uio_device_id_template),
		      id);
}

static int get_subdevice(nic_t *nic,
			 uint32_t *id)
{
	return get_id(nic,
		      uio_subdevice_id_template,
		      sizeof(uio_subdevice_id_template),
		      id);
}

int get_bus_slot_func_num(nic_t *nic,
			  uint32_t *bus,
			  uint32_t *slot,
			  uint32_t *func)
{
	size_t size;
	char *path, *tok, *tok2;
	int path_tokens, i;
	size_t path_size;
	char *read_pci_bus_slot_func_str;
	char pci_bus_slot_func_str[32];
	int rc;
	char *saveptr;

	path_size = sizeof(uio_device_symlink_template) + 4;
	path = malloc(path_size);
	if(path == NULL)
	{
		LOG_ERR("Could not allocate path memory for %s",
			uio_device_symlink_template);
		rc = -ENOMEM;
		goto error_alloc_path;
	}

	read_pci_bus_slot_func_str = malloc(128);
	if(read_pci_bus_slot_func_str == NULL)
	{
		LOG_ERR(PFX "Could not allocate read pci bus memory for %s",
			nic->log_name, uio_device_symlink_template);
		rc = -ENOMEM;
		goto error_alloc_read_pci_bus;
	}

	snprintf(path, path_size, uio_device_symlink_template, nic->uio_minor);

	size = readlink(path, read_pci_bus_slot_func_str, 128);
	if(size == -1) {
		LOG_ERR(PFX "%s: Error with %s: %s",
			nic->log_name, path, strerror(errno));
		rc = errno;
		goto error;
	}

	if(size > ((128) - 1)) {
		read_pci_bus_slot_func_str[128 - 1] = '\0';
		LOG_ERR(PFX "%s: not enough space (%d) for reading PCI "
			    "slot:bus.func %s: %s",
			    nic->log_name, size, path, strerror(errno));
		rc = -EIO;
		goto error;
	}

	/*  readlink() doesn't NULL terminate the string */
	read_pci_bus_slot_func_str[size] = '\0';

	path_tokens = 0;
	tok = strtok_r(read_pci_bus_slot_func_str, "/", &saveptr);
	while (tok != NULL) {
		path_tokens++;
		tok = strtok_r(NULL, "/", &saveptr);
	}

	size = readlink(path, read_pci_bus_slot_func_str, 128);
	if(size == -1) {
		LOG_ERR(PFX "%s: Error with %s: %s",
			nic->log_name, path, strerror(errno));
		rc = errno;
		goto error;
	}

	if(size > ((128) - 1)) {
		read_pci_bus_slot_func_str[128 - 1] = '\0';
		LOG_ERR(PFX "%s: not enough space for reading PCI "
			    "slot:bus.func %s: %s",
			    nic->log_name, path, strerror(errno));
		rc = -EIO;
		goto error;
	}

	/*  readlink() doesn't NULL terminate the string */
	read_pci_bus_slot_func_str[size] = '\0';

	tok = strtok_r(read_pci_bus_slot_func_str, "/", &saveptr);
	for (i=0; i<path_tokens-1; i++) {
		tok = strtok_r(NULL, "/", &saveptr);
	}
	strcpy(pci_bus_slot_func_str, tok);

	tok = strtok_r(pci_bus_slot_func_str, ":", &saveptr);
	if(tok == NULL) {
		LOG_ERR(PFX "%s: Error with slot string: %s",
			nic->log_name, pci_bus_slot_func_str);
		rc = -EIO;
		goto error;
	}

	tok = strtok_r(NULL, ":", &saveptr);
	if(tok == NULL) {
		LOG_ERR(PFX "%s: Error parsing slot: %s",
			nic->log_name, pci_bus_slot_func_str);
		rc = -EIO;
		goto error;
	}

	sscanf(tok, "%x", bus);

	/*  Need to extract the next token "xx.x" */
	tok = strtok_r(NULL, ":", &saveptr);
	if(tok == NULL) {
		LOG_ERR(PFX "%s: Error extracing bus.func: %s",
			nic->log_name, pci_bus_slot_func_str);
		rc = -EIO;
		goto error;
	}

	tok2 = strtok_r(tok, ".", &saveptr);
	if(tok2 == NULL) {
		LOG_ERR(PFX "%s: Error parsing bus: %s",
			nic->log_name, pci_bus_slot_func_str);
		rc = -EIO;
		goto error;
	}

	sscanf(tok2, "%x", slot);

	tok2 = strtok_r(NULL, ".", &saveptr);
	if(tok2 == NULL) {
		LOG_ERR(PFX "%s: Error parsing func: %s",
			nic->log_name, pci_bus_slot_func_str);
		rc = -EIO;
		goto error;
	}

	sscanf(tok2, "%x", func);
	LOG_INFO(PFX "%s: is found at %02x:%02x.%02x", nic->log_name,
		 *bus, *slot, *func);

	free(read_pci_bus_slot_func_str);

	rc = 0;

error:
error_alloc_read_pci_bus:
	free(path);

error_alloc_path:

	return rc;
}

/**
 *  find_set_nic_lib() - Match the NIC library to the NIC
 *  @param nic - NIC device to determine which NIC library to use
 *  @return 0 on success <0 on failure
 */
int find_set_nic_lib(nic_t *nic)
{
	uint32_t vendor;
	uint32_t subvendor;
	uint32_t device;
	uint32_t subdevice;

	uint32_t pci_bus;
	uint32_t pci_slot;
	uint32_t pci_func;
	int rc = 0;

	nic_lib_handle_t *handle;
	struct pci_device_id *pci_entry;

	rc = get_vendor(nic, &vendor);
	if( rc != 0)
	{
		LOG_ERR("%s: Could not get vendor id [0x%x]",
			nic->log_name, rc);
		return rc;
	}

	rc = get_subvendor(nic, &subvendor);
	if( rc != 0)
	{
		LOG_ERR("%s: Could not get subvendor id [0x%x]",
			nic->log_name, rc);
		return rc;
	}

	rc = get_device(nic, &device);
	if( rc != 0)
	{
		LOG_ERR("%s: Could not get device id [0x%x]",
			nic->log_name, rc);
		return rc;
	}

	rc = get_subdevice(nic, &subdevice);
	if( rc != 0)
	{
		LOG_ERR("%s: Could not get subdevice id [0x%x]",
			nic->log_name, rc);
		return rc;
	}

	get_bus_slot_func_num(nic, &pci_bus, &pci_slot, &pci_func);

	LOG_DEBUG("%s: Looking for device vendor: "
		  "0x%x subvendor: 0x%x device: 0x%x subdevice: 0x%x",
		  nic->log_name, vendor, subvendor, device, subdevice);

	rc = find_nic_lib_using_pci_id(vendor, device, subvendor, subdevice,
				       &handle, &pci_entry);

	if(rc != 0) {
		LOG_WARN(PFX "%s: Couldn't find proper NIC library",
			 nic->log_name);
		return rc;
	}

	nic->nic_library = handle;
	nic->pci_id      = pci_entry;

	/*  Prepare the NIC library op table */
	nic->ops = handle->ops;

	return 0;
}
