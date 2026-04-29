/*
 * Copyright (c) 2026 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <memfault/core/platform/device_info.h>

#include "dfu_target.h"

void memfault_platform_get_device_info(sMemfaultDeviceInfo *info)
{
	*info = (sMemfaultDeviceInfo){
		.device_serial = "dummy_mesh_serial",
		.software_type = dfu_target_memfault_software_type_get(),
		.software_version = dfu_target_memfault_software_version_get(),
		.hardware_version = dfu_target_memfault_hardware_version_get(),
	};
}
