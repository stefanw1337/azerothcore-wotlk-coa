/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 */

#ifndef GROUP_SHARED_SLOTS_H
#define GROUP_SHARED_SLOTS_H

#include "ObjectGuid.h"

class Group;

// A module tells the core which player rides in another member's body (Two-Players heads)
void SetBodyOfRider(ObjectGuid (*bodyOfRider)(ObjectGuid rider));
ObjectGuid BodyOfRider(ObjectGuid rider);
uint32 SharedGroupSlots(Group const* group);
bool GroupHasRoomFor(Group const* group, ObjectGuid joiner);

#endif
