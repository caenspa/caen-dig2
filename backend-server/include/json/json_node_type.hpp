/******************************************************************************
*
*	CAEN SpA - Software Division
*	Via Vetraia, 11 - 55049 - Viareggio ITALY
*	+39 0594 388 398 - www.caen.it
*
*******************************************************************************
*
*	Copyright (C) 2020-2023 CAEN SpA
*
*	This file is part of the CAEN Back-end Server.
*
*	The CAEN Back-end Server is free software; you can redistribute it and/or
*	modify it under the terms of the GNU Lesser General Public
*	License as published by the Free Software Foundation; either
*	version 3 of the License, or (at your option) any later version.
*
*	TheCAEN Back-end Server is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
*	Lesser General Public License for more details.
*
*	You should have received a copy of the GNU Lesser General Public
*	License along with the CAEN Back-end Server; if not, see
*	https://www.gnu.org/licenses/.
*
*	SPDX-License-Identifier: LGPL-3.0-or-later
*
***************************************************************************//*!
*
*	\file		json_node_type.hpp
*	\brief
*	\author		Giovanni Cerretani
*
******************************************************************************/

#ifndef CAEN_INCLUDE_JSON_JSON_NODE_TYPE_HPP_
#define CAEN_INCLUDE_JSON_JSON_NODE_TYPE_HPP_

#include <nlohmann/json.hpp>

#include "json/json_common.hpp"

// To be defined in global namespace, as CAEN_FELib_NodeType_t is in a C header
NLOHMANN_JSON_SERIALIZE_ENUM(nt::node_type, {
	{ nt::node_type::CAEN_FELib_UNKNOWN, 	nullptr 		},
	{ nt::node_type::CAEN_FELib_PARAMETER,	"PARAMETER"		},
	{ nt::node_type::CAEN_FELib_COMMAND,	"COMMAND"		},
	{ nt::node_type::CAEN_FELib_FEATURE,	"FEATURE"		},
	{ nt::node_type::CAEN_FELib_ATTRIBUTE,	"ATTRIBUTE"		},
	{ nt::node_type::CAEN_FELib_ENDPOINT,	"ENDPOINT"		},
	{ nt::node_type::CAEN_FELib_CHANNEL,	"CHANNEL"		},
	{ nt::node_type::CAEN_FELib_DIGITIZER,	"DIGITIZER"		},
	{ nt::node_type::CAEN_FELib_FOLDER,		"FOLDER"		},
	{ nt::node_type::CAEN_FELib_LVDS,		"LVDS"			},
	{ nt::node_type::CAEN_FELib_VGA,		"VGA"			},
	{ nt::node_type::CAEN_FELib_HV_CHANNEL,	"HV_CHANNEL"	},
	{ nt::node_type::CAEN_FELib_MONOUT,		"MONOUT"		},
	{ nt::node_type::CAEN_FELib_VTRACE,		"VTRACE"		},
	{ nt::node_type::CAEN_FELib_GROUP,		"GROUP"			},
})

#endif /* CAEN_INCLUDE_JSON_JSON_NODE_TYPE_HPP_ */
