from Options import Choice, Toggle, DeathLink, Range, PerGameCommonOptions, OptionSet, OptionGroup
from dataclasses import dataclass

class StartingCharacter(Choice):
    """
    Select which character you start with. The remaining characters 
    will be shuffled into the item pool as unlockable items.
    """
    display_name = "Starting Character"
    option_sonic = 0
    option_cream = 1
    option_tails = 2
    option_knuckles = 3
    option_amy = 4
    default = 0

class LockZones(Toggle):
    """
    If enabled, you must find Zone Unlock items to progress to subsequent zones.
    If disabled, zones unlock normally as you beat previous bosses.
    """
    display_name = "Lock Zones"
    default = True

class StartingZone(Choice):
    """
    Select which zone you start with. The chosen starting zone will be unlocked 
    by default. The remaining zones' unlock items will be shuffled into the item pool.
    """
    display_name = "Starting Zone"
    option_leaf_forest = 0
    option_hot_crater = 1
    option_music_plant = 2
    option_ice_paradise = 3
    option_sky_canyon = 4
    option_techno_base = 5
    option_egg_utopia = 6
    default = 0

sa2_options_groups = [
    OptionGroup("Game Play",[
        StartingCharacter,
        LockZones,
        StartingZone,
    ])
]

@dataclass
class SA2Options(PerGameCommonOptions):
    start_char: StartingCharacter
    lock_zones: LockZones
    start_zone: StartingZone