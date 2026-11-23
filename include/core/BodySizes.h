#pragma once

#include <string>

// Published body radius of every troop and building. Read by
// CardFactories::applyCardMetadata into Entity::bodyRadius, used wherever a
// body matters: body-to-body contact (Board::resolveCollisions,
// Board::resolvePositionAgainstBuildings), attack reach and sight
// (CombatEntity::effectiveRadiusOf), the nearest-footprint ranking of
// targets, area hits that are measured to the body.
//
// Until patch 0057 every troop was one size, Entity::IMPLICIT_TROOP_RADIUS =
// 0.4 tiles: a Skeleton and a Golem took up the same room, two Giants walked
// 0.8 tiles apart, and a Hog Rider placed one tile from an Ice Golem never
// touched it. The game's unit records give each body a CollisionRadius, from
// 400 (the spirits, Wall Breakers) to 1000 (Sparky, Skeleton King). Every
// building was 1.0 (Building::COLLISION_RADIUS); the records give 500 (Tesla,
// Goblin Drill), 600 (Cannon, Bomb Tower, Inferno Tower, X-Bow, Mortar) and
// 1000 (the huts, Tombstone, Goblin Cage, Elixir Collector).
//
// Source: FirstLight-Zero card_specs.json, release 15.535.13, the
// CollisionRadius of the unit record each card summons, in thousandths of a
// tile. Range and sight are body to body in the same records (sight equals
// range for the Musketeer, Wizard and Cannon), so reach is range + own radius
// + target radius with these radii.
//
// Keyed by registry NAME, like LoadTimes.h: an Evolution inherits the row, and
// "Hero X" with no row of its own has the body of X. Two cards hold two
// different bodies under one name, so those rows carry the registry id of the
// second body: the Rascal Girls (-28, 500; the Boy is 750) and Goblinstein's
// Doctor (-40, 500; the Monster is 750).
//
// NO PUBLISHED RECORD in this release's pack: Golemite, Lava Pups, Elixir
// Golemite, Elixir Blob, Goblin Brawler, Cursed Hog, Minion Giant, and the
// units the hero abilities summon (Guardienne, Skeletrooper, Rhino, Tomb
// Queen, Decoy, Lumberjack Ghost, Runner, Skeleton Shadow, Souldier). They
// take DEFAULT_RADIUS = 0.5, the value of 101 of the 158 published forms.
// The two ability buildings with no record (Trusty Turret, Snowman) take
// DEFAULT_BUILDING_RADIUS = 0.6, the body of every published building that
// shoots except the Tesla.
//
// NOT covered: the Crown Towers (Tower::getCollisionRadius, 1.5 and 2.0), for
// which this pack has no record, and the footprint a new card must keep clear
// of an existing building (Entity::getPlacementRadius, still 1.0).
//
// A troop or building built by hand with no card (the test fixtures) keeps
// Entity::IMPLICIT_TROOP_RADIUS / Building::COLLISION_RADIUS.
namespace BodySizes {

constexpr float DEFAULT_RADIUS = 0.5f;
constexpr float DEFAULT_BUILDING_RADIUS = 0.6f;

struct Row { const char* name; int id; int radiusThousandths; };

// id 0 = every card of that name; otherwise only that registry id.
inline constexpr Row ROWS[] = {
    { "Rascals", -28, 500 },           // RascalGirl
    { "Goblinstein", -40, 500 },       // the Doctor
    { "Ice Spirit", 0, 400 }, { "Fire Spirit", 0, 400 }, { "Heal Spirit", 0, 400 },
    { "Electro Spirit", 0, 400 }, { "Wall Breakers", 0, 400 },
    { "Mini PEKKA", 0, 450 }, { "Hero Mini P.E.K.K.A.", 0, 450 }, { "Berserker", 0, 450 },
    { "Knight", 0, 500 }, { "Archers", 0, 500 }, { "Goblins", 0, 500 }, { "Minions", 0, 500 },
    { "Minion Horde", 0, 500 }, { "Balloon", 0, 500 }, { "Witch", 0, 500 }, { "Barbarians", 0, 500 },
    { "Skeletons", 0, 500 }, { "Skeleton Army", 0, 500 }, { "Valkyrie", 0, 500 }, { "Bomber", 0, 500 },
    { "Musketeer", 0, 500 }, { "Three Musketeers", 0, 500 }, { "Baby Dragon", 0, 500 },
    { "Wizard", 0, 500 }, { "Spear Goblins", 0, 500 }, { "Ice Wizard", 0, 500 }, { "Guards", 0, 500 },
    { "Princess", 0, 500 }, { "Miner", 0, 500 }, { "Lumberjack", 0, 500 }, { "Inferno Dragon", 0, 500 },
    { "Dart Goblin", 0, 500 }, { "Electro Wizard", 0, 500 }, { "Elite Barbarians", 0, 500 },
    { "Royal Recruits", 0, 500 }, { "Night Witch", 0, 500 }, { "Bats", 0, 500 }, { "Cannon Cart", 0, 500 },
    { "Skeleton Barrel", 0, 500 }, { "Flying Machine", 0, 500 }, { "Fisherman", 0, 500 },
    { "Firecracker", 0, 500 }, { "Mighty Miner", 0, 500 }, { "Battle Healer", 0, 500 },
    { "Archer Queen", 0, 500 }, { "Monk", 0, 500 }, { "Mother Witch", 0, 500 }, { "Phoenix", 0, 500 },
    { "Little Prince", 0, 500 }, { "Spirit Empress", 0, 500 }, { "Suspicious Bush", 0, 500 },
    { "Bush Goblins", 0, 500 }, { "Goblin Gang", 0, 500 },
    { "Prince", 0, 600 }, { "Hog Rider", 0, 600 }, { "Dark Prince", 0, 600 }, { "Mega Minion", 0, 600 },
    { "Hunter", 0, 600 }, { "Executioner", 0, 600 }, { "Bandit", 0, 600 }, { "Royal Ghost", 0, 600 },
    { "Ram Rider", 0, 600 }, { "Zappies", 0, 600 }, { "Royal Hogs", 0, 600 }, { "Magic Archer", 0, 600 },
    { "Electro Dragon", 0, 600 }, { "Elixir Golem", 0, 600 }, { "Goblin Demolisher", 0, 600 },
    { "Kamikaze Goblin Demolisher", 0, 600 }, { "Boss Bandit", 0, 600 }, { "Ronin", 0, 600 },
    { "Furnace", 0, 600 },
    { "Ice Golem", 0, 700 },
    { "Giant", 0, 750 }, { "P.E.K.K.A.", 0, 750 }, { "Golem", 0, 750 }, { "Giant Skeleton", 0, 750 },
    { "Royal Giant", 0, 750 }, { "Lava Hound", 0, 750 }, { "Bowler", 0, 750 }, { "Battle Ram", 0, 750 },
    { "Rascals", 0, 750 }, { "Mega Knight", 0, 750 }, { "Goblin Giant", 0, 750 },
    { "Electro Giant", 0, 750 }, { "Goblin Machine", 0, 750 }, { "Goblinstein", 0, 750 },
    { "Rune Giant", 0, 750 },
    { "Golden Knight", 0, 800 },
    { "Skeleton Dragons", 0, 900 },
    { "Sparky", 0, 1000 }, { "Skeleton King", 0, 1000 },
    // Buildings.
    { "Tesla", 0, 500 }, { "Goblin Drill", 0, 500 },
    { "Cannon", 0, 600 }, { "Bomb Tower", 0, 600 }, { "Inferno Tower", 0, 600 }, { "X-Bow", 0, 600 },
    { "Mortar", 0, 600 },
    { "Barbarian Hut", 0, 1000 }, { "Goblin Hut", 0, 1000 }, { "Tombstone", 0, 1000 },
    { "Goblin Cage", 0, 1000 }, { "Elixir Collector", 0, 1000 },
};

inline const Row* findExact(const std::string& name, int id) {
    for (const Row& r : ROWS) {
        if (r.id != 0 && r.id == id && name == r.name) return &r;
    }
    for (const Row& r : ROWS) {
        if (r.id == 0 && name == r.name) return &r;
    }
    return nullptr;
}

// The row of a card: its own, or the plain card's for a "Hero X".
inline const Row* find(const std::string& name, int id) {
    if (const Row* r = findExact(name, id)) return r;
    static const std::string HERO = "Hero ";
    if (name.compare(0, HERO.size(), HERO) == 0) return findExact(name.substr(HERO.size()), id);
    return nullptr;
}

// Published CollisionRadius in thousandths of a tile; 0 = no row.
inline int publishedRadius(const std::string& name, int id) {
    const Row* r = find(name, id);
    return r ? r->radiusThousandths : 0;
}

// Body radius in tiles of a card's troop or building.
inline float radius(const std::string& name, int id, bool building = false) {
    const Row* r = find(name, id);
    if (r) return r->radiusThousandths / 1000.0f;
    return building ? DEFAULT_BUILDING_RADIUS : DEFAULT_RADIUS;
}

}  // namespace BodySizes
