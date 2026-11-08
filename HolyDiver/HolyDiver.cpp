/*
*
* Holy diver - an epic adventure at object-oriented world way beneath the surface!
*
*/

/*Tested only on windows and should be c++17 friendly. No found platform specific stuff.
TextMap.txt MUST BE IN THE SAME FOLDER THAN THIS FILE */

#include <algorithm> //clamp()
#include <cctype>    //tolower()
#include <fstream>   //map loading
#include <iostream>  //cin, cout, isteam, ostream
#include <limits>    //numeric_limits<int>::max() used for map dimentions
#include <memory>    //unique_ptr<World> for automatic memory cleanup
#include <random>    //random
#include <stdexcept> //reports invalid data
#include <string>    //map rows, file paths, commands, and displayed messages
#include <utility>   //move()
#include <vector>    //rows, enemies, revealed cells, available spawn positions, and collision flags

using namespace std;

/*********************************
             Helpers
**********************************/

// value representing a location on the map
struct Position
{
    int x = 0; int y = 0;
};

bool operator==(Position a, Position b)
{
    return a.x == b.x && a.y == b.y;
}

enum class Direction
{
    Up,
    Down,
    Left,
    Right
};

Position adjacent(Position position, Direction direction)
{
    switch (direction)
    {
    case Direction::Up:    --position.y; break;
    case Direction::Down:  ++position.y; break;
    case Direction::Left:  --position.x; break;
    case Direction::Right: ++position.x; break;
    }

    return position;
}

enum class CommandType
{
    Move,
    Illuminate,
    ToggleMapVisibility,
    StartNewGame,
    Restart,
    Quit,
    Invalid
};

// value describing an action independently of keyboard input
struct Command
{
    CommandType type = CommandType::Invalid;
    Direction direction = Direction::Up;
};

// used to choose input based on gameplay state
// see Game -> void Run()
enum class InputContext
{
    Playing,
    GameOver
};

enum class EnemyMovement
{
    Stationary,
    RandomWalker
};

struct EnemySpawn
{
    Position position;
    EnemyMovement movement = EnemyMovement::Stationary;
};

// Parsed level data. Terrain contains only water and walls
struct LevelData
{
    vector<string> terrain;
    Position playerSpawn;
    vector<EnemySpawn> enemies;
};

enum class DeathCause
{
    None,
    Injuries,
    OxygenDepleted
};

enum class ActionOutcome
{
    Ignored,
    Moved,
    MovementBlocked,
    OutsideMap,
    InsufficientBattery,
    Illuminated
};

// Gameplay result containing facts for Game and ConsoleUI
struct TurnResult
{
    ActionOutcome outcome = ActionOutcome::Ignored;
    int damage = 0;
    DeathCause deathCause = DeathCause::None;
};

struct PlayerStats
{
    int health = 0;
    int oxygen = 0;
    int battery = 0;
};

enum class TerrainTile
{
    Wall,
    Water
};

// What each cell can contain
enum class CellContent
{
    Outside,
    Hidden,
    Wall,
    Water,
    Player,
    StationaryEnemy,
    RandomWalkerEnemy
};

/*********************************
          Game objects
**********************************/

// PLAYER

// Responsibility:
// - own the player's position and resources
// - keep resource values within their limits
// - determine whether the player is alive.

// World owns the live Player and validates destinations before calling moveTo.

class Player
{

public:

    static constexpr int Maximum = 100;

    explicit Player(Position spawn) : _position(spawn) {}

    Position position() const { return _position; }

    int health() const { return _health; }
    int oxygen() const { return _oxygen; }
    int battery() const { return _battery; }

    PlayerStats stats() const { return { _health, _oxygen, _battery }; }

    bool isAlive() const
    {
        return deathCause() == DeathCause::None;
    }

    DeathCause deathCause() const
    {
        if (_health <= 0) return DeathCause::Injuries;
        if (_oxygen <= 0) return DeathCause::OxygenDepleted;
        return DeathCause::None;
    }

    void moveTo(Position destination)
    {
        _position = destination;
    }

    void takeDamage(int amount)
    {
        _health -= clamp(amount, 0, _health);
    }

    void consumeOxygen(int amount)
    {
        _oxygen -= clamp(amount, 0, _oxygen);
    }

    bool tryConsumeBattery(int amount)
    {
        if (amount < 0 || amount > _battery)
            return false;

        _battery -= amount;
        return true;
    }

    // Resource restoration is redy for adding items to the game
    void replenishOxygen(int amount)
    {
        _oxygen += clamp(amount, 0, Maximum - _oxygen);
    }

    void restoreHealth(int amount)
    {
        _health += clamp(amount, 0, Maximum - _health);
    }

    void rechargeBattery(int amount)
    {
        _battery += clamp(amount, 0, Maximum - _battery);
    }

private:

    Position _position;
    int _health = Maximum, _oxygen = Maximum, _battery = Maximum;
};

// ENEMY

// Responsibility:
// - own an enemy's position, activation state, damage value, and movement behavior
// - world validates and applies proposed movement

class Enemy
{

public:

    Enemy(Position position, EnemyMovement movement)
        : _position(position), _movement(movement)
    {
        if (movement != EnemyMovement::Stationary && movement != EnemyMovement::RandomWalker)
            throw runtime_error("Invalid enemy movement.");
    }

    Position position() const { return _position; }

    EnemyMovement movement() const { return _movement; }

    bool isActive() const { return _active; }

    void activate() { _active = true; }

    int damage() const { return 10; }

    void moveTo(Position destination)
    {
        _position = destination;
    }

    Position chooseMove(mt19937& random) const
    {
        if (!_active || _movement == EnemyMovement::Stationary)
            return _position;

        // Four directions plus a chance to stay still.
        int choice = uniform_int_distribution<int>(0, 4)(random);
        if (choice == 4)
            return _position;

        return adjacent(_position, static_cast<Direction>(choice));
    }

private:

    Position _position;
    EnemyMovement _movement;
    bool _active = false;
};

// TERRAIN

// Responsibility:
// - own and validate the static grid map
// - answer terrain and boundary queries
// - it knows nothing about actors, visibility, or console output

class Terrain
{

public:

    explicit Terrain(vector<string> rows) : _rows(move(rows))
    {
        if (_rows.empty() || _rows.front().empty())
            throw runtime_error("Empty terrain.");

        const auto maximum = static_cast<size_t>(numeric_limits<int>::max());
        if (_rows.size() > maximum || _rows.front().size() > maximum)
            throw runtime_error("Map is too large.");

        for (const auto& row : _rows)
            if (row.size() != _rows.front().size() || row.find_first_not_of("ox") != string::npos)
                throw runtime_error("Invalid terrain.");
    }

    int width() const { return static_cast<int>(_rows.front().size()); }

    int height() const { return static_cast<int>(_rows.size()); }

    bool isInside(Position p) const
    {
        return p.x >= 0 && p.y >= 0 && p.x < width() && p.y < height();
    }

    bool isWalkable(Position p) const
    {
        return isInside(p) && _rows[p.y][p.x] == 'o';
    }

    TerrainTile tileAt(Position p) const
    {
        if (!isInside(p))
            throw out_of_range("Position is outside the terrain.");

        return _rows[p.y][p.x] == 'o' ? TerrainTile::Water : TerrainTile::Wall;
    }

private:

    vector<string> _rows;
};

/*********************************
          Level loading
**********************************/

//LevelData

// Responsibility:
// - translate the map-file format into level data

namespace MapLoader
{
    LevelData read(istream& input)
    {
        LevelData level;
        string row;
        bool foundPlayer = false;

        while (getline(input, row))
        {
            if (!row.empty() && row.back() == '\r')
                row.pop_back();

            if (row.empty())
                throw runtime_error("Map has an empty row.");

            const auto maximum = static_cast<size_t>(numeric_limits<int>::max());
            if (row.size() > maximum || level.terrain.size() >= maximum)
                throw runtime_error("Map is too large.");

            int y = static_cast<int>(level.terrain.size());

            for (size_t x = 0; x < row.size(); ++x)
            {
                Position p{ static_cast<int>(x), y };
                switch (row[x])
                {
                case 'x':
                case 'o':
                    break;

                case 'P':
                    if (foundPlayer)
                        throw runtime_error("Map needs exactly one P.");

                    level.playerSpawn = p; foundPlayer = true; row[x] = 'o'; break;

                case 'm':
                    level.enemies.push_back({ p, EnemyMovement::Stationary }); row[x] = 'o'; break;

                case 'M':
                    level.enemies.push_back({ p, EnemyMovement::RandomWalker }); row[x] = 'o'; break;

                default:
                    throw runtime_error("Unknown map symbol.");
                }
            }

            level.terrain.push_back(move(row));
        }

        if (input.bad() || (input.fail() && !input.eof()))
            throw runtime_error("Could not finish reading map.");

        if (level.terrain.empty() || !foundPlayer)
            throw runtime_error("Map needs rows and exactly one P.");

        return level;
    }

    LevelData load(const string& path)
    {
        ifstream file(path);
        if (!file) throw runtime_error("Could not open map: " + path);
        return read(file);
    }
}

/*********************************
           World rules
**********************************/

// Responsibility: 
// - own the live actors and exploration state
// - validate actor placement
// - coordinate movement, illumination, and collision rules
// - it returns gameplay facts

class World
{

public:

    explicit World(LevelData level, unsigned seed = random_device{}())
        : _terrain(move(level.terrain)), _player(level.playerSpawn), _random(seed)
    {
        if (!_terrain.isWalkable(_player.position()))
            throw runtime_error("Invalid player spawn.");

        _revealed.assign(height(), vector<bool>(width(), false));

        for (const auto& spawn : level.enemies)
        {
            if (!_terrain.isWalkable(spawn.position) || spawn.position == _player.position() || occupied(spawn.position))
                throw runtime_error("Invalid enemy spawn.");

            _enemies.emplace_back(spawn.position, spawn.movement);
        }

        reveal(_player.position());
    }

    int width() const { return _terrain.width(); }

    int height() const { return _terrain.height(); }

    PlayerStats playerStats() const { return _player.stats(); }

    bool isPlayerAlive() const { return _player.isAlive(); }

    CellContent cellAt(Position p, bool showWholeMap = false) const
    {
        if (!_terrain.isInside(p)) return CellContent::Outside;
        if (p == _player.position()) return CellContent::Player;
        if (!showWholeMap && !_revealed[p.y][p.x]) return CellContent::Hidden;

        for (const auto& enemy : _enemies)
            if (enemy.position() == p)
                return enemy.movement() == EnemyMovement::Stationary
                ? CellContent::StationaryEnemy : CellContent::RandomWalkerEnemy;

        return _terrain.tileAt(p) == TerrainTile::Water ? CellContent::Water : CellContent::Wall;
    }

    void addEnemyIfMissing(EnemyMovement movement)
    {
        if (movement != EnemyMovement::Stationary && movement != EnemyMovement::RandomWalker)
            throw runtime_error("Invalid enemy movement.");

        for (const auto& enemy : _enemies)
            if (enemy.movement() == movement)
                return;

        vector<Position> free;

        for (int y = 0; y < height(); ++y) for (int x = 0; x < width(); ++x)
        {
            Position p{ x, y };
            if (_terrain.isWalkable(p) && !(p == _player.position()) && !occupied(p))
                free.push_back(p);
        }

        // tiny maps may have no room for another enemy
        if (free.empty())
            return;

        auto index = uniform_int_distribution<size_t>(0, free.size() - 1)(_random);
        Position spawn = free[index];
        _enemies.emplace_back(spawn, movement);

        if (_revealed[spawn.y][spawn.x])
            _enemies.back().activate();
    }

    TurnResult takeTurn(Command command)
    {
        TurnResult result;
        if (!_player.isAlive() || (command.type != CommandType::Move && command.type != CommandType::Illuminate))
            return result;

        // every gameplay attempt uses oxygen also blocked attempts.
        _player.consumeOxygen(OxygenPerTurn);

        if (!_player.isAlive())
        {
            result.deathCause = _player.deathCause();
            return result;
        }

        Position target = adjacent(_player.position(), command.direction);
        result.outcome = command.type == CommandType::Move ? movePlayer(target) : illuminate(target);

        // each enemy can hit at most once during this turn.
        vector<bool> hit(_enemies.size(), false);
        result.damage = collide(hit);

        if (_player.isAlive())
        {
            moveEnemies();
            result.damage += collide(hit);
        }

        result.deathCause = _player.deathCause();
        return result;
    }

private:

    static constexpr int OxygenPerTurn = 2;
    static constexpr int IlluminationCost = 5;

    ActionOutcome movePlayer(Position target)
    {
        if (!_terrain.isWalkable(target))
            return ActionOutcome::MovementBlocked;

        _player.moveTo(target);
        reveal(target);
        return ActionOutcome::Moved;
    }

    ActionOutcome illuminate(Position target)
    {
        if (!_terrain.isInside(target))
            return ActionOutcome::OutsideMap;

        if (!_player.tryConsumeBattery(IlluminationCost))
            return ActionOutcome::InsufficientBattery;

        reveal(target);
        return ActionOutcome::Illuminated;
    }

    void moveEnemies()
    {
        for (auto& enemy : _enemies)
        {
            Position next = enemy.chooseMove(_random);

            if (_terrain.isWalkable(next) && !occupied(next))
                enemy.moveTo(next);
        }
    }

    bool occupied(Position p) const
    {
        for (const auto& enemy : _enemies)
            if (enemy.position() == p)
                return true;

        return false;
    }

    void reveal(Position p)
    {
        _revealed[p.y][p.x] = true;

        for (auto& enemy : _enemies)
            if (enemy.position() == p)
                enemy.activate();
    }

    int collide(vector<bool>& hit)
    {
        int damage = 0;
        for (size_t i = 0; i < _enemies.size(); ++i)
        {
            if (!hit[i] && _enemies[i].position() == _player.position())
            {
                _enemies[i].activate();
                int attackDamage = _enemies[i].damage();
                _player.takeDamage(attackDamage);
                damage += attackDamage;
                hit[i] = true;
            }
        }

        return damage;
    }

    Terrain _terrain;
    vector<vector<bool>> _revealed;
    Player _player;
    vector<Enemy> _enemies;
    mt19937 _random;
};

//LEVEL POPULATION

// Responsibility:
// - define the default enemies explicitly

namespace LevelPopulation
{
    void addDefaults(World& world)
    {
        world.addEnemyIfMissing(EnemyMovement::Stationary);
        world.addEnemyIfMissing(EnemyMovement::RandomWalker);
    }
}

/*********************************
           Console UI
**********************************/

// Responsibility: 
// - read console input
// - define commands for the current menu
// - format all world results and cell contents for display
// - the map visibility toggle affects visuals only, never exploration.
// - cool af ASCII art

class ConsoleUI
{

public:

    explicit ConsoleUI(istream& input = cin, ostream& output = cout)
        : _input(input), _output(output) {
    }

    void DisplayMainMenu() const
    {
        _output << R"ASCII(
              ,.  _~-.,               .
           ~'`_ \/,_. \_
          / ,"_>@`,__`~.)             |           .
          | |  @@@@'  ",! .           .          '
          |/   ^^@     .!  \          |         /
          `' .^^^     ,'    '         |        .             .
           .^^^   .          \                /          .
          .^^^       '  .     \       |      /       . '
.,.,.     ^^^             ` .   .,+~'`^`'~+,.     , '
&&&&&&,  ,^^^^.  . ._ ..__ _  .'             '. '_ __ ____ __ _ .. .  .
%%%%%%%%%^^^^^^%%&&;_,.-=~'`^`'~=-.,__,.-=~'`^`'~=-.,__,.-=~'`^`'~=-.,
&&&&&%%%%%%%%%%%%%%%%%%&&;,.-=~'`^`'~=-.,__,.-=~'`^`'~=-.,__,.-=~'`^`'~=
%%%%%&&&&&&&&&&&%%%%&&&_,.;^`'~=-.,__,.-=~'`^`'~=-.,__,.-=~'`^`'~=-.,__,
%%%%%%%%%&&&&&&&&&-=~'`^`'~=-.,__,.-=~'`^`'~=-.,__,.-==--^'~=-.,__,.-=~'
#######*"'=-.,_,.-=~'`^`'~=-.,__,.-=~'`^`'~=-.,__,.-=~'`^`'~=-.,.-=~'`^`
'~=-.,__,.-=~`'^`'~=-.,__,.-=~'`^`'~=-.,__,.-=~'`^`'~=-.,__,.-=~'`^`'~=-
)ASCII";

        _output << "\n";
        _output << "~~~~~~~~~~ HOLY DIVER ~~~~~~~~~~\n";
        _output << "        0 - Start Game\n";
        _output << "        Q - Quit Game\n";
        _output << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n";
    }

    void DisplayDeathScreen() const
    {
        _output << R"ASCII(
   )\.-.      /`-.    )\   )\   )\.---.          .-./(       .-.   )\.---.     /`-.
 ,' ,-,_)   ,' _  \  (  ',/ /  (   ,-._(       ,'     )  ,'  /  ) (   ,-._(  ,' _  \
(  .   __  (  '-' (   )    (    \  '-,        (  .-, (  (  ) | (   \  '-,   (  '-' (
 ) '._\ _)  )   _  ) (  \(\ \    ) ,-`         ) '._\ )  ) './ /    ) ,-`    ) ,_ .'
(  ,   (   (  ,' ) \  `.) /  )  (  ``-.       (  ,   (  (  ,  (    (  ``-.  (  ' ) \
 )/'._.'    )/    )/      '.(    )..-.(        )/ ._.'   )/..'      )..-.(   )/   )/
                                                                                     )ASCII";

        _output << "\n";
        _output << "~~~~~~~~~~ YOU DIED! ~~~~~~~~~~\n";
        _output << "        0 - New Game\n";
        _output << "        Q - Quit Game\n";
        _output << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n";
    }

    void showControls() const
    {
        showMessage("HOLY DIVER | WASD move | IJKL light | 0 toggle full map (dev) | R restart | Q quit | Enter confirms");
    }

    static Command decode(char key, InputContext context = InputContext::Playing)
    {
        int normalized = tolower(static_cast<unsigned char>(key));
        if (normalized == 'q') return { CommandType::Quit };

        if (context == InputContext::GameOver)
            return normalized == '0' ? Command{ CommandType::StartNewGame } : Command{};

        switch (normalized)
        {
        case 'w': return { CommandType::Move, Direction::Up };
        case 's': return { CommandType::Move, Direction::Down };
        case 'a': return { CommandType::Move, Direction::Left };
        case 'd': return { CommandType::Move, Direction::Right };
        case 'i': return { CommandType::Illuminate, Direction::Up };
        case 'k': return { CommandType::Illuminate, Direction::Down };
        case 'j': return { CommandType::Illuminate, Direction::Left };
        case 'l': return { CommandType::Illuminate, Direction::Right };
        case '0': return { CommandType::ToggleMapVisibility };
        case 'r': return { CommandType::Restart };
        default: return {};
        }
    }

    Command readCommand(InputContext context = InputContext::Playing) const
    {
        _output << ">>> ";
        string line;

        if (!getline(_input, line))
            return { CommandType::Quit };

        return line.empty() ? Command{} : decode(line.front(), context);
    }

    bool readMapPath(string& path) const
    {
        _output << "Map path (0 = TextMap.txt, Q = quit): ";

        if (!getline(_input, path) || path == "q" || path == "Q")
            return false;

        if (path == "0")
            path = "TextMap.txt";

        return true;
    }

    void showMessage(const string& message) const
    {
        if (!message.empty())
            _output << message << '\n';
    }

    void showTurnResult(const TurnResult& result) const
    {
        string message;
        switch (result.outcome)
        {
        case ActionOutcome::MovementBlocked:     message = "Movement blocked."; break;
        case ActionOutcome::OutsideMap:          message = "No tile in that direction."; break;
        case ActionOutcome::InsufficientBattery: message = "Not enough battery."; break;
        case ActionOutcome::Illuminated:         message = "Tile illuminated."; break;
        case ActionOutcome::Ignored:
        case ActionOutcome::Moved:
            break;
        }

        if (result.damage > 0)
        {
            if (!message.empty()) message += ' ';
            message += "Damage taken: " + to_string(result.damage);
        }

        showMessage(message);
    }

    void showDeathCause(DeathCause cause) const
    {
        switch (cause)
        {
        case DeathCause::Injuries:       showMessage("You died from injuries."); break;
        case DeathCause::OxygenDepleted: showMessage("You ran out of oxygen."); break;
        case DeathCause::None:           break;
        }
    }

    void toggleMapVisibility()
    {
        _showWholeMap = !_showWholeMap;
        showMessage(_showWholeMap ? "Full map visibility ON (dev)." : "Full map visibility OFF (dev).");
    }

    void resetMapVisibility() { _showWholeMap = false; }

    void render(const World& world, int lives) const
    {
        _output << '\n';
        for (int y = 0; y < world.height(); ++y)
        {
            for (int x = 0; x < world.width(); ++x)
                _output << symbol(world.cellAt({ x, y }, _showWholeMap)) << ' ';

            _output << '\n';
        }

        PlayerStats stats = world.playerStats();

        _output << "Health: " << stats.health << '\n'
            << "Oxygen: " << stats.oxygen << '\n'
            << "Battery: " << stats.battery << '\n'
            << "Lives: " << lives << "\n\n";
    }

private:

    static char symbol(CellContent content)
    {
        switch (content)
        {
        case CellContent::Outside:           return '?';
        case CellContent::Hidden:            return ' ';
        case CellContent::Wall:              return 'x';
        case CellContent::Water:             return 'o';
        case CellContent::Player:            return 'P';
        case CellContent::StationaryEnemy:   return 'm';
        case CellContent::RandomWalkerEnemy: return 'M';
        }

        return '?';
    }

    istream& _input;
    ostream& _output;
    bool _showWholeMap = false;
};

/*********************************
           Game session
**********************************/

// Responsibility
// - manage the session loop, level selection, lives, restarts
// - handle transitions between playing and game-over input

// ConsoleUI handles input/output; World handles gameplay; level-loading and
// population functions build replacements before the current world is discarded

class Game
{

public:

    explicit Game(istream& input = cin, ostream& output = cout)
        : _ui(input, output) {
    }

    void run()
    {
        _ui.DisplayMainMenu();
        _ui.showControls();

        if (!chooseLevel()) return;

        _ui.render(*_world, _lives);

        while (true)
        {
            bool gameOver = _lives == 0;
            if (gameOver)
                _ui.DisplayDeathScreen();

            Command command = _ui.readCommand(gameOver ? InputContext::GameOver : InputContext::Playing);
            if (command.type == CommandType::Quit)
                break;

            if (gameOver)
                handleGameOverCommand(command);
            else
                handleGameplayCommand(command);
        }
    }

private:

    static constexpr int StartingLives = 3;

    static unique_ptr<World> createWorld(const string& path)
    {
        auto world = make_unique<World>(MapLoader::load(path));
        LevelPopulation::addDefaults(*world);
        return world;
    }

    bool chooseLevel()
    {
        string path;
        while (_ui.readMapPath(path))
        {
            try
            {
                auto candidate = createWorld(path);
                _mapPath = path;
                _world = move(candidate);
                _lives = StartingLives;
                _ui.resetMapVisibility();
                return true;
            }

            catch (const exception& error) { _ui.showMessage(error.what()); }
        }

        return false;
    }

    bool reload()
    {
        try
        {
            // keep the current world and UI state unless replacement succeeds
            auto candidate = createWorld(_mapPath);
            _world = move(candidate);
            _ui.resetMapVisibility();
            return true;
        }

        catch (const exception& error) { _ui.showMessage(error.what()); return false; }
    }

    void handleGameOverCommand(Command command)
    {
        if (command.type != CommandType::StartNewGame)
        {
            _ui.showMessage("Press 0 for a new game or Q to quit.");
            return;
        }

        if (reload())
        {
            _lives = StartingLives;
            _ui.showMessage("New game started.");
            _ui.render(*_world, _lives);
        }
    }

    void handleGameplayCommand(Command command)
    {
        switch (command.type)
        {
        case CommandType::Restart:
            if (reload())
            {
                _lives = StartingLives;
                _ui.showMessage("Game restarted.");
            }
            break;

        case CommandType::ToggleMapVisibility:
            _ui.toggleMapVisibility();
            break;

        case CommandType::Move:
        case CommandType::Illuminate:
            if (!_world->isPlayerAlive())
            {
                _ui.showMessage("Press R to restart or Q to quit.");
                break;
            }

            processTurn(command);
            break;

        default:
            return;
        }

        if (_lives > 0)
            _ui.render(*_world, _lives);
    }

    void processTurn(Command command)
    {
        TurnResult result = _world->takeTurn(command);
        _ui.showTurnResult(result);

        if (result.deathCause != DeathCause::None)
            handlePlayerDeath(result.deathCause);
    }

    void handlePlayerDeath(DeathCause cause)
    {
        _ui.showDeathCause(cause);
        --_lives;

        if (_lives > 0 && reload())
            _ui.showMessage("Life lost. Level restarted.");
    }

    ConsoleUI _ui;
    unique_ptr<World> _world;
    string _mapPath;
    int _lives = StartingLives;
};

// define HOLYDIVER_TEST when including this file in a separate test program
#ifndef HOLYDIVER_TEST
int main() { Game game; game.run(); }
#endif
