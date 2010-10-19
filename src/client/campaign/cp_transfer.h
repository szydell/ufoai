/**
 * @file cp_transfer.h
 * @brief Header file for Transfer stuff.
 */

/*
Copyright (C) 2002-2010 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#ifndef CLIENT_CL_TRANSFER_H
#define CLIENT_CL_TRANSFER_H

#define MAX_TRANSFERS	16

struct transferData_s;

enum {
	CARGO_TYPE_INVALID = 0,
	CARGO_TYPE_ITEM,
	CARGO_TYPE_EMPLOYEE,
	CARGO_TYPE_ALIEN_DEAD,
	CARGO_TYPE_ALIEN_ALIVE,
	CARGO_TYPE_AIRCRAFT,

	CARGO_TYPE_MAX
};

enum {
	TRANS_ALIEN_ALIVE,
	TRANS_ALIEN_DEAD,

	TRANS_ALIEN_MAX
};

/** @brief Transfer informations (they are being stored in ccs.transfers[MAX_TRANSFERS]. */
typedef struct transfer_s {
	qboolean active;				/**< True if this transfer is under processing. */
	base_t *destBase;				/**< Pointer to destination base. May not be NULL if active is true. */
	base_t *srcBase;				/**< Pointer to source base. May be NULL if transfer comes from a mission (alien body recovery). */
	date_t event;					/**< When the transfer finish process should start. */

	int itemAmount[MAX_OBJDEFS];			/**< Amount of given item. */
	int alienAmount[MAX_TEAMDEFS][TRANS_ALIEN_MAX];		/**< Alien cargo, [0] alive, [1] dead. */
	struct employee_s *employeeArray[MAX_EMPL][MAX_EMPLOYEES];	/**< List of personal transferring. */
	linkedList_t *aircraft;

	qboolean hasItems;				/**< Transfer of items. */
	qboolean hasEmployees;			/**< Transfer of employees. */
	qboolean hasAliens;				/**< Transfer of aliens. */
	qboolean hasAircraft;			/**< Transfer of aircraft. */
} transfer_t;

/** @brief Array of current cargo onboard. */
typedef struct transferCargo_s {
	int type;			/**< Type of cargo (1 - items, 2 - employees, 3 - alien bodies, 4 - live aliens). */
	int itemidx;			/**< Index of item in cargo. */
} transferCargo_t;


/**
 * @brief transfer types
 */
typedef enum {
	TRANS_TYPE_INVALID = -1,
	TRANS_TYPE_ITEM,
	TRANS_TYPE_EMPLOYEE,
	TRANS_TYPE_ALIEN,
	TRANS_TYPE_AIRCRAFT,

	TRANS_TYPE_MAX
} transferType_t;

typedef struct transferData_s {
	/** @brief Current selected aircraft for transfer (if transfer started from mission). */
	aircraft_t *transferStartAircraft;

	/** @brief Current selected base for transfer. */
	base_t *transferBase;

	/** @brief Current transfer type (item, employee, alien, aircraft). */
	transferType_t currentTransferType;

	/** @brief Current cargo onboard. */
	transferCargo_t cargo[MAX_CARGO];

	/** @brief Current item cargo. Amount of items for each object definition index. */
	int trItemsTmp[MAX_OBJDEFS];

	/** @brief Current alien cargo. [0] alive [1] dead */
	int trAliensTmp[MAX_TEAMDEFS][TRANS_ALIEN_MAX];

	/** @brief Current personnel cargo. */
	employee_t *trEmployeesTmp[MAX_EMPL][MAX_EMPLOYEES];

	/** @brief Current aircraft for transfer. */
	linkedList_t *aircraft;

	/** @brief Current cargo type count (updated in TR_CargoList) */
	int trCargoCountTmp;
} transferData_t;

void TR_TransferCheck(void);
void TR_NotifyAircraftRemoved(const aircraft_t *aircraft);

void TR_InitStartup(void);

qboolean TR_TransferStart(base_t *srcBase, transferData_t *transData);
void TR_TransferAlienAfterMissionStart(const base_t *base, aircraft_t *transferAircraft);

transfer_t* TR_GetNext(transfer_t *lastTransfer);

#endif /* CLIENT_CL_TRANSFER_H */
