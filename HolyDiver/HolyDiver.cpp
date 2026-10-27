/*
*
* Holy diver - an epic adventure at object-oriented world way beneath the surface!
*
*/

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

/*********************************
             Helpers
**********************************/

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
    Restart,
    Quit,
    Invalid
};

struct Command
{
    CommandType type = CommandType::Invalid;
    Direction direction = Direction::Up;
};

enum class EnemyMovement 
{
    Stationary,
    RandomWalker
};

struct EnemySpawn
{
    Position position;
    EnemyMovement movement;
};

struct LevelData 
{
    // Terrain never contains player or enemy characters.
    vector<string> terrain;
    Position playerSpawn;
    vector<EnemySpawn> enemies;
};


// Player stores resources. Only World can change its position.
class Player
{

public:
    
    static constexpr int Maximum = 100;

    explicit Player(Position spawn) : _position(spawn) {}
    
    Position position() const { return _position; }

    int health() const { return _health; }
    int oxygen() const { return _oxygen; }
    int battery() const { return _battery; }

    bool isAlive() const
    {
        return _health > 0 && _oxygen > 0;
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

    // These functions for when items are added.
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

    friend class World;
    
    Position _position;
    
    int _health = Maximum, _oxygen = Maximum, _battery = Maximum;
};


// Enemies suggest moves; World checks whether those moves are allowed.
class Enemy 
{

public:

    Enemy(Position position, EnemyMovement movement)
        : _position(position), _movement(movement) {}

    Position position() const { return _position; }
    
    bool isActive() const { return _active; }
    
    void activate() { _active = true; }
    
    int giveDamage() const { return 10; }
    
    char symbol() const 
    {
        return _movement == EnemyMovement::Stationary ? 'm' : 'M';
    }
    
    Position chooseMove(mt19937& random) const 
    {
        if (!_active || _movement == EnemyMovement::Stationary)
            return _position;

        // Four directions plus a chance to stay still.
        int choice = uniform_int_distribution<int>(0, 4)(random);
        if (choice == 4) return _position;
        return adjacent(_position, static_cast<Direction>(choice));
    }

private:
    
    friend class World;
    
    Position _position;
    EnemyMovement _movement;
    bool _active = false;
};

// The loader only reads files. It never writes to the map.
class MapLoader 
{

public:
    
    static LevelData load(const string& path)
    {
        ifstream file(path);
        if (!file) throw runtime_error("Could not open map: " + path);
        return read(file);
    }
    
    static LevelData read(istream& input)
    {
        LevelData level;
        string row;
        int players = 0;
        size_t width = 0;

        while (getline(input, row))
        {
            if (!row.empty() && row.back() == '\r')
                row.pop_back();

            if (row.empty())
                throw runtime_error("Map has an empty row.");
            
            if (level.terrain.empty())
                width = row.size();
            
            if (row.size() != width)
                throw runtime_error("Map is not rectangular.");
            
            if (row.size() > static_cast<size_t>(numeric_limits<int>::max()) ||
                level.terrain.size() >= static_cast<size_t>(numeric_limits<int>::max()))
                throw runtime_error("Map is too large.");
            
            int y = static_cast<int>(level.terrain.size());
            
            for (size_t x = 0; x < row.size(); ++x)
            {
                Position p{static_cast<int>(x), y};
                switch (row[x])
                {
                case 'x': 
                
                case 'o':
                    break;
                
                case 'P':
                    level.playerSpawn = p; ++players; row[x] = 'o'; break;
                
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
        
        if (level.terrain.empty() || players != 1)
            throw runtime_error("Map needs rows and exactly one P.");
        
        return level;
    }
};


// World owns the terrain, visibility, player, and enemies.
class World 
{

public:

    explicit World(LevelData level, unsigned seed = random_device{}())
        : _terrain(move(level.terrain)), _player(level.playerSpawn), _random(seed)
    {
        // Validate even when LevelData was created without MapLoader.
        if (_terrain.empty() || _terrain.front().empty())
            throw runtime_error("Empty world.");
        
        for (const auto& row : _terrain)
            
            if (row.size() != _terrain.front().size() || row.find_first_not_of("ox") != string::npos)
                throw runtime_error("Invalid terrain.");
        
        if (!isWalkable(_player.position()))
            throw runtime_error("Invalid player spawn.");
        
        _revealed.assign(_terrain.size(), vector<bool>(_terrain.front().size(), false));
        
        for (const auto& spawn : level.enemies) 
        {
            if (!isWalkable(spawn.position) || spawn.position == _player.position() || occupied(spawn.position))
                throw runtime_error("Invalid enemy spawn.");
            
            _enemies.emplace_back(spawn.position, spawn.movement);
        }
        
        // Existing maps need no editing: supply a missing enemy type automatically.
        addMissingEnemy(EnemyMovement::Stationary, 'm');
        addMissingEnemy(EnemyMovement::RandomWalker, 'M');
        reveal(_player.position());
    }
    
    int width() const { return static_cast<int>(_terrain.front().size()); }
    
    int height() const { return static_cast<int>(_terrain.size()); }
    
    const Player& player() const { return _player; }
    
    const vector<Enemy>& enemies() const { return _enemies; }
    
    const vector<string>& terrain() const { return _terrain; }
    
    bool isInside(Position p) const { return p.x >= 0 && p.y >= 0 && p.x < width() && p.y < height(); }
    
    bool isWalkable(Position p) const { return isInside(p) && _terrain[p.y][p.x] == 'o'; }
    
    // Development view leaves exploration and enemy activation unchanged.
    void toggleMapVisibility() { _showWholeMap = !_showWholeMap; }

    bool isWholeMapVisible() const { return _showWholeMap; }

    char symbolAt(Position p) const
    {
        if (!isInside(p)) return '?';
        if (p == _player.position()) return 'P';
        if (!_showWholeMap && !_revealed[p.y][p.x]) return ' ';
        for (const auto& enemy : _enemies) if (enemy.position() == p) return enemy.symbol();
        return _terrain[p.y][p.x];
    }

    string takeTurn(Command command) 
    {
        if (!_player.isAlive() || (command.type != CommandType::Move && command.type != CommandType::Illuminate))
            return "";
        
        // Every gameplay attempt uses oxygen, even when blocked.
        _player.consumeOxygen(2);
        
        if (!_player.isAlive())
            return "You ran out of oxygen.";
        
        Position target = adjacent(_player.position(), command.direction);
        
        string message;
        
        if (command.type == CommandType::Move)
        {
            if (isWalkable(target)) { _player._position = target; reveal(target); }
            else message = "Movement blocked.";
        } else {
            if (!isInside(target)) message = "No tile in that direction.";
            else if (!_player.tryConsumeBattery(5))
                message = "Not enough battery.";
            else { reveal(target); message = "Tile illuminated."; }
        }

        // A collision before and after enemy movement must not cause two hits.
        vector<bool> hit(_enemies.size(), false);
        int damage = collide(hit);
        
        if (_player.isAlive()) 
        {
            for (auto& enemy : _enemies) 
            {
                Position next = enemy.chooseMove(_random);
                if (isWalkable(next) && !occupied(next)) enemy._position = next;
            }

            damage += collide(hit);
        }

        if (damage) message += " Damage taken: " + to_string(damage);
        
        return message;
    }

private:
    
    bool occupied(Position p) const
    {
        for (const auto& enemy : _enemies) if (enemy.position() == p) return true;
        return false;
    }

    void addMissingEnemy(EnemyMovement movement, char symbol)
    {
        for (const auto& enemy : _enemies) if (enemy.symbol() == symbol) return;
        
        vector<Position> free;
        
        for (int y = 0; y < height(); ++y) for (int x = 0; x < width(); ++x)
        {
            Position p{x, y};
            if (isWalkable(p) && !(p == _player.position()) && !occupied(p)) free.push_back(p);
        }

        // Tiny maps may have no room for another enemy.
        if (free.empty()) return;
        auto index = uniform_int_distribution<size_t>(0, free.size() - 1)(_random);
        _enemies.emplace_back(free[index], movement);
    }

    void reveal(Position p)
    {
        _revealed[p.y][p.x] = true;
        for (auto& enemy : _enemies) if (enemy.position() == p) enemy.activate();
    }

    int collide(vector<bool>& hit)
    {
        int damage = 0;
        for (size_t i = 0; i < _enemies.size(); ++i)
        {
            if (!hit[i] && _enemies[i].position() == _player.position())
            {
                _enemies[i].activate();
                _player.takeDamage(_enemies[i].giveDamage());
                damage += _enemies[i].giveDamage();
                hit[i] = true;
            }
        }

        return damage;
    }

    // Containers free their own memory when the world is replaced or destroyed.
    vector<string> _terrain;
    vector<vector<bool>> _revealed;
    bool _showWholeMap = false;
    Player _player;
    vector<Enemy> _enemies;
    mt19937 _random;
};

// ConsoleUI reads commands and displays data; it does not change the world.
class ConsoleUI
{

public:
    
    void DisplayMainMenu() const 
    {
        cout << R"ASCII(
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

        cout << "\n";
        cout << "~~~~~~~~~~ HOLY DIVER ~~~~~~~~~~\n";
        cout << "        0 - Start Game\n";
        cout << "        Q - Quit Game\n";
        cout << "~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n\n";
    }

    static Command decode(char key)
    {
        switch (tolower(static_cast<unsigned char>(key)))
        {
        case 'w': return {CommandType::Move, Direction::Up};
        case 's': return {CommandType::Move, Direction::Down};
        case 'a': return {CommandType::Move, Direction::Left};
        case 'd': return {CommandType::Move, Direction::Right};
        case 'i': return {CommandType::Illuminate, Direction::Up};
        case 'k': return {CommandType::Illuminate, Direction::Down};
        case 'j': return {CommandType::Illuminate, Direction::Left};
        case 'l': return {CommandType::Illuminate, Direction::Right};
        case '0': return {CommandType::ToggleMapVisibility};
        case 'r': return {CommandType::Restart};
        case 'q': return {CommandType::Quit};
        default: return {};
        }
    }

    Command readCommand() const
    {
        cout << ">>> ";
        string line;
        
        if (!getline(cin, line))
            return {CommandType::Quit};
        
        return line.empty() ? Command{} : decode(line.front());
    }

    void showMessage(const string& message) const
    {
        if (!message.empty()) cout << message << '\n';
    }

    void render(const World& world, int lives) const
    {
        cout << '\n';
        for (int y = 0; y < world.height(); ++y)
        {
            for (int x = 0; x < world.width(); ++x) cout << world.symbolAt({x, y}) << ' ';
            cout << '\n';
        }

        const auto& p = world.player();
        
        cout << "Health: " << p.health() << endl
             << "Oxygen: " << p.oxygen() << endl
             << "Battery: " << p.battery() << endl
             << "Lives: " << lives << "\n\n";
    }
};

// Game controls the session and replaces the whole world on restart.
class Game
{

public:
    
    void run()
    {
        _ui.DisplayMainMenu();

        _ui.showMessage("HOLY DIVER | WASD move | IJKL light | 0 toggle full map (dev) | R restart | Q quit | Enter confirms");

        if (!chooseLevel()) return;

        _ui.render(*_world, _lives);
        
        while (true)
        {
            Command command = _ui.readCommand();

            if (command.type == CommandType::Quit)
                break;

            if (command.type == CommandType::Restart)
            {
                if (reload()) { _lives = 3; _ui.showMessage("Game restarted."); }
            }
            
            else if (command.type == CommandType::ToggleMapVisibility)
            {
                _world->toggleMapVisibility();
                _ui.showMessage(_world->isWholeMapVisible() ? "Full map visibility ON (dev)." : "Full map visibility OFF (dev).");
            }

            else if (command.type == CommandType::Invalid)
                continue;

            else if (!_world->player().isAlive())
                _ui.showMessage("Press R to restart or Q to quit.");
            
            else
            {
                _ui.showMessage(_world->takeTurn(command));
                
                if (!_world->player().isAlive())
                {
                    _ui.showMessage(_world->player().health() <= 0 ? "You died from injuries." : "You ran out of oxygen.");
                    --_lives;
                    if (_lives > 0) { if (reload()) _ui.showMessage("Life lost. Level restarted."); }
                    else _ui.showMessage("GAME OVER. R restarts; Q quits.");
                }
            }
            
            _ui.render(*_world, _lives);
        }
    }

private:

    bool chooseLevel()
    {
        while (true)
        {
            cout << "Map path (0 = TextMap.txt, Q = quit): ";
            string path;
            
            if (!getline(cin, path) || path == "q" || path == "Q")
                return false;
            
            if (path == "0")
                path = "TextMap.txt";
            
            try
            {
                auto candidate = make_unique<World>(MapLoader::load(path));
                _mapPath = path;
                _world = move(candidate);
                return true;
            }
            
            catch (const exception& error) { _ui.showMessage(error.what()); }
        }
    }
    
    bool reload()
    {
        try
        {
            // Do not discard the current world unless replacement succeeds.
            auto candidate = make_unique<World>(MapLoader::load(_mapPath));
            _world = move(candidate);
            return true;
        }
        
        catch (const exception& error) { _ui.showMessage(error.what()); return false; }
    }

    ConsoleUI _ui;
    unique_ptr<World> _world;
    string _mapPath;
    int _lives = 3;
};

// Tests can include these classes without creating a second main function.
//Codex tests defined through compiler?
#ifndef HOLYDIVER_TEST
int main() { Game game; game.run(); }
#endif
