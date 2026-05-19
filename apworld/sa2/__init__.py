from BaseClasses import Region, Item, Location, ItemClassification
from worlds.AutoWorld import World
from .Locations import location_table, ZONES
from .Items import item_table
from .Options import sa2_options_groups, SA2Options
from .Rules import set_rules

class SonicAdvance2Item(Item):
    game: str = "Sonic Advance 2"

class SonicAdvance2Location(Location):
    game: str = "Sonic Advance 2"

class SonicAdvance2World(World):
    """
    Sonic Advance 2 is a fast-paced 2D platformer. 
    Race through 7 zones, collect Chaos Emeralds, and defeat Dr. Eggman!
    """
    game = "Sonic Advance 2"
    topology_present = False
    data_version = 1
    
    item_name_to_id = item_table
    location_name_to_id = location_table
    options_dataclass = SA2Options
    
    def create_items(self):
        pool = []
        
        # 1. Characters to unlock
        char_names = ["Sonic", "Cream", "Tails", "Knuckles", "Amy"]
        starting_char_idx = self.options.start_char.value
        starting_char_name = char_names[starting_char_idx]
        skip_item_name = f"Unlock {starting_char_name}"
        
        for item_name in ["Unlock Sonic", "Unlock Cream", "Unlock Tails", "Unlock Knuckles", "Unlock Amy"]:
            if item_name != skip_item_name:
                item_id = self.item_name_to_id[item_name]
                pool.append(SonicAdvance2Item(item_name, ItemClassification.progression, item_id, self.player))
                
        # 2. Chaos Emeralds
        for i in range(7):
            item_name = f"Chaos Emerald {i+1}"
            item_id = self.item_name_to_id[item_name]
            pool.append(SonicAdvance2Item(item_name, ItemClassification.progression, item_id, self.player))
            
        # 2.5. Zone Unlock items (if option enabled)
        if self.options.lock_zones:
            zone_item_names = [
                "Unlock Leaf Forest",
                "Unlock Hot Crater",
                "Unlock Music Plant",
                "Unlock Ice Paradise",
                "Unlock Sky Canyon",
                "Unlock Techno Base",
                "Unlock Egg Utopia",
            ]
            starting_zone_idx = self.options.start_zone.value
            skip_zone_item = zone_item_names[starting_zone_idx]
            
            for zone_idx, item_name in enumerate(zone_item_names):
                if item_name != skip_zone_item:
                    item_id = self.item_name_to_id[item_name]
                    pool.append(SonicAdvance2Item(item_name, ItemClassification.progression, item_id, self.player))
            
            # Always add Unlock XX for the final zone
            xx_item_id = self.item_name_to_id["Unlock XX"]
            pool.append(SonicAdvance2Item("Unlock XX", ItemClassification.progression, xx_item_id, self.player))
            
        # 3. Fill the rest of the pool with filler items (10 Rings, 1-Up)
        total_locations = len(self.location_name_to_id)
        filler_options = ["10 Rings", "1-Up"]
        
        needed_fillers = total_locations - len(pool)
        for i in range(needed_fillers):
            filler_name = filler_options[i % len(filler_options)]
            item_id = self.item_name_to_id[filler_name]
            pool.append(SonicAdvance2Item(filler_name, ItemClassification.filler, item_id, self.player))
            
        self.multiworld.itempool.extend(pool)
            
    def get_filler_item_name(self) -> str:
        return self.multiworld.random.choice(["10 Rings", "1-Up"])

    def create_regions(self):
        menu = Region("Menu", self.player, self.multiworld)
        self.multiworld.regions.append(menu)
        
        # Add all locations to the main region
        for loc_name, loc_id in self.location_name_to_id.items():
            loc = SonicAdvance2Location(self.player, loc_name, loc_id, menu)
            menu.locations.append(loc)
            
    def set_rules(self):
        set_rules(self)

    def fill_slot_data(self) -> dict:
        return {
            "start_char": self.options.start_char.value,
            "start_zone": self.options.start_zone.value,
            "lock_zones": self.options.lock_zones.value,
        }
