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
//
// MASS (patch 0058). The same unit records give every troop a Mass, from 1
// (Skeletons, the spirits, Bats, Spear Goblins, Guards) to 20 (Golem);
// buildings have none (0 here) and are never moved. Board::resolveCollisions
// shares the overlap of two touching troops by it: each moves the other's
// mass over the sum of the two, so the heavier body gives way less. The
// records publish the masses, NOT the rule; the split is the ordinary
// two-body one and is checked against one recorded pair (a Hog Rider, mass 4,
// behind an Ice Golem, mass 6: see the engine notes of patch 0058). A body
// with no record takes DEFAULT_MASS = 5, the median of the published troops,
// and so does a troop built by hand (Entity::IMPLICIT_TROOP_MASS).
namespace BodySizes {

constexpr float DEFAULT_RADIUS = 0.5f;
constexpr float DEFAULT_BUILDING_RADIUS = 0.6f;
constexpr float DEFAULT_MASS = 5.0f;

struct Row { const char* name; int id; int radiusThousandths; int mass; };

// id 0 = every card of that name; otherwise only that registry id.
inline constexpr Row ROWS[] = {
    { "Rascals", -28, 500, 2 },           // RascalGirl
    { "Goblinstein", -40, 500, 4 },       // the Doctor
    { "Ice Spirit", 0, 400, 1 }, { "Fire Spirit", 0, 400, 1 }, { "Heal Spirit", 0, 400, 1 },
    { "Electro Spirit", 0, 400, 1 }, { "Wall Breakers", 0, 400, 4 },
    { "Mini PEKKA", 0, 450, 4 }, { "Hero Mini P.E.K.K.A.", 0, 450, 4 }, { "Berserker", 0, 450, 2 },
    { "Knight", 0, 500, 6 }, { "Archers", 0, 500, 3 }, { "Goblins", 0, 500, 2 }, { "Minions", 0, 500, 2 },
    { "Minion Horde", 0, 500, 2 }, { "Balloon", 0, 500, 6 }, { "Witch", 0, 500, 8 }, { "Barbarians", 0, 500, 4 },
    { "Skeletons", 0, 500, 1 }, { "Skeleton Army", 0, 500, 1 }, { "Valkyrie", 0, 500, 5 }, { "Bomber", 0, 500, 4 },
    { "Musketeer", 0, 500, 5 }, { "Three Musketeers", 0, 500, 5 }, { "Baby Dragon", 0, 500, 5 },
    { "Wizard", 0, 500, 5 }, { "Spear Goblins", 0, 500, 1 }, { "Ice Wizard", 0, 500, 5 }, { "Guards", 0, 500, 1 },
    { "Princess", 0, 500, 3 }, { "Miner", 0, 500, 6 }, { "Lumberjack", 0, 500, 4 }, { "Inferno Dragon", 0, 500, 5 },
    { "Dart Goblin", 0, 500, 3 }, { "Electro Wizard", 0, 500, 5 }, { "Elite Barbarians", 0, 500, 4 },
    { "Royal Recruits", 0, 500, 5 }, { "Night Witch", 0, 500, 4 }, { "Bats", 0, 500, 1 }, { "Cannon Cart", 0, 500, 3 },
    { "Skeleton Barrel", 0, 500, 7 }, { "Flying Machine", 0, 500, 3 }, { "Fisherman", 0, 500, 10 },
    { "Firecracker", 0, 500, 6 }, { "Mighty Miner", 0, 500, 6 }, { "Battle Healer", 0, 500, 6 },
    { "Archer Queen", 0, 500, 6 }, { "Monk", 0, 500, 6 }, { "Mother Witch", 0, 500, 6 }, { "Phoenix", 0, 500, 2 },
    { "Little Prince", 0, 500, 4 }, { "Spirit Empress", 0, 500, 5 }, { "Suspicious Bush", 0, 500, 2 },
    { "Bush Goblins", 0, 500, 2 }, { "Goblin Gang", 0, 500, 2 },
    { "Prince", 0, 600, 6 }, { "Hog Rider", 0, 600, 4 }, { "Dark Prince", 0, 600, 6 }, { "Mega Minion", 0, 600, 6 },
    { "Hunter", 0, 600, 5 }, { "Executioner", 0, 600, 4 }, { "Bandit", 0, 600, 3 }, { "Royal Ghost", 0, 600, 3 },
    { "Ram Rider", 0, 600, 4 }, { "Zappies", 0, 600, 4 }, { "Royal Hogs", 0, 600, 2 }, { "Magic Archer", 0, 600, 3 },
    { "Electro Dragon", 0, 600, 7 }, { "Elixir Golem", 0, 600, 15 }, { "Goblin Demolisher", 0, 600, 6 },
    { "Kamikaze Goblin Demolisher", 0, 600, 6 }, { "Boss Bandit", 0, 600, 4 }, { "Ronin", 0, 600, 6 },
    { "Furnace", 0, 600, 6 },
    { "Ice Golem", 0, 700, 6 },
    { "Giant", 0, 750, 18 }, { "P.E.K.K.A.", 0, 750, 18 }, { "Golem", 0, 750, 20 }, { "Giant Skeleton", 0, 750, 18 },
    { "Royal Giant", 0, 750, 18 }, { "Lava Hound", 0, 750, 5 }, { "Bowler", 0, 750, 18 }, { "Battle Ram", 0, 750, 6 },
    { "Rascals", 0, 750, 10 }, { "Mega Knight", 0, 750, 18 }, { "Goblin Giant", 0, 750, 18 },
    { "Electro Giant", 0, 750, 18 }, { "Goblin Machine", 0, 750, 18 }, { "Goblinstein", 0, 750, 18 },
    { "Rune Giant", 0, 750, 18 },
    { "Golden Knight", 0, 800, 10 },
    { "Skeleton Dragons", 0, 900, 4 },
    { "Sparky", 0, 1000, 18 }, { "Skeleton King", 0, 1000, 10 },
    // Buildings.
    { "Tesla", 0, 500, 0 }, { "Goblin Drill", 0, 500, 0 },
    { "Cannon", 0, 600, 0 }, { "Bomb Tower", 0, 600, 0 }, { "Inferno Tower", 0, 600, 0 }, { "X-Bow", 0, 600, 0 },
    { "Mortar", 0, 600, 0 },
    { "Barbarian Hut", 0, 1000, 0 }, { "Goblin Hut", 0, 1000, 0 }, { "Tombstone", 0, 1000, 0 },
    { "Goblin Cage", 0, 1000, 0 }, { "Elixir Collector", 0, 1000, 0 },
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

// Published Mass; 0 = no row, or a building (no Mass is published for one).
inline int publishedMass(const std::string& name, int id) {
    const Row* r = find(name, id);
    return r ? r->mass : 0;
}

// Mass of a card's troop: the published one, or DEFAULT_MASS.
inline float mass(const std::string& name, int id) {
    const int m = publishedMass(name, id);
    return m > 0 ? static_cast<float>(m) : DEFAULT_MASS;
}

}  // namespace BodySizes
