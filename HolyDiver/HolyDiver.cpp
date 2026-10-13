/*
*
* Holy diver - an epic adventure at object-oriented world way beneath the surface!
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <iostream>
#include <fstream>

using namespace std;


/****************************************************/
// Functions
/****************************************************/
void start_splash_screen(void);
void startup_routines(void);
void quit_routines(void);

int load_level(string);     // Changed form void -> int to allow it to return stuff

int read_input(char*);
void update_state(char);
void render_screen(void);
void free_map(void);


/****************************************************/
// Globals
/****************************************************/
char** map = nullptr;

// We need to remember the allocated dimensions.
// render_screen() and quit_routines() need these.
size_t map_width = 0;
size_t map_height = 0;

string current_Level_path;

const int MAX_HEALTH = 100;
const int MAX_OXYGEN = 100;
const int INIT_LIVES = 3;


typedef struct Player {

    // Stats
    int health;
    int oxygen;
    int lives;

    // Position
    size_t pos_x;
    size_t pos_y;

} Player;


Player player_data = { MAX_HEALTH, MAX_OXYGEN, INIT_LIVES, 0, 0 };


/****************************************************************
 *
 * MAIN
 *
 ****************************************************************/
int main(void)
{
    start_splash_screen();
    startup_routines();

    // Show the initial map immediately after loading it.
    render_screen();

    char input;

    while (true)
    {
        input = '\0';

        if (0 > read_input(&input))
            break;

        update_state(input);
        render_screen();
    }

    quit_routines();

    return 0;
}


/****************************************************************
 *
 * FUNCTION load_level
 *
 * Loads a rectangular text map into dynamically allocated memory.
 * 
 * You can just type "TextMap.txt" because the map is in the root of the folder
 *
 * Returns:
 *   0  = success
 *  -1  = could not open file
 *  -2  = empty file
 *  -3  = map is not rectangular
 *
 ****************************************************************/
int load_level(string filepath)
{
    ifstream file(filepath);

    if (!file.is_open())
    {
        cout << "ERROR: Could not open file: " << filepath << endl;
        return -1;
    }


    string line;

    /************************************************************
     * PASS 1
     *
     * Determine map dimensions.
     ************************************************************/

    map_width = 0;
    map_height = 0;


    if (!getline(file, line))
    {
        cout << "ERROR: Map file is empty." << endl;
        file.close();
        return -2;
    }


    // Width is determined from first row.
    map_width = line.length();
    map_height = 1;


    // Check remaining rows.
    while (getline(file, line))
    {
        if ((int)line.length() != map_width)
        {
            cout << "ERROR: Map is not rectangular." << endl;

            map_width = 0;
            map_height = 0;

            file.close();
            return -3;
        }

        map_height++;
    }


    /************************************************************
     * Allocate dynamic 2D array.
     *
     * First:
     *
     * map
     *  |
     *  v
     * [char*]
     * [char*]
     * [char*]
     *    ...
     *
     * Then each char* gets its own row:
     *
     * map[0] -> [c][c][c][c]...
     * map[1] -> [c][c][c][c]...
     * map[2] -> [c][c][c][c]...
     *
     ************************************************************/

    map = new char* [map_height];

    for (size_t y = 0; y < map_height; y++)
    {
        map[y] = new char[map_width];
    }


    /************************************************************
     * PASS 2
     *
     * Go back to beginning and copy map characters into memory.
     ************************************************************/

    file.clear();
    file.seekg(0, ios::beg);


    size_t y = 0;

    while (y < map_height && getline(file, line))
    {
        if ((int)line.length() != map_width)
        {
            cout << "ERROR: Map changed while loading." << endl;
            return -3;
        }

        for (size_t x = 0; x < map_width; x++)
        {
            map[y][x] = line[x];

            if (line[x] == 'P')
            {
                player_data.pos_x = x;
                player_data.pos_y = y;
            }
        }

        y++;
    }


    file.close();


    cout << endl;
    cout << "Map loaded successfully." << endl;
    cout << "Map size: " << map_width << " x " << map_height << endl;


    return 0;
}


/****************************************************************
 *
 * FUNCTION read_input
 *
 ****************************************************************/
int read_input(char* input)
{
    string line;

    cout << ">>> ";

    getline(cin, line);

    cout << endl;

    // No input entered
    if (line.empty())
        return 0;

    // Only use the first character typed
    *input = line[0];

    if (*input == 'q' || *input == 'Q')
        return -2;

    return 0;
}


/****************************************************************
 *
 * FUNCTION update_state
 *
 ****************************************************************/
void update_state(char input)
{

    // Reload current level
    if (input == 'r' || input == 'R')
    {
        free_map();

        int result = load_level(current_Level_path);

        if (result == 0)
        {
            player_data.oxygen = MAX_OXYGEN;
            cout << "Level restarted." << endl;
        }

        return;
    }

    // New player position
    int new_x = (int)player_data.pos_x;
    int new_y = (int)player_data.pos_y;

    // Oxygen loss
    int oxygen_loss = 2;

    switch (input)
    {
    case 'w':
    case 'W':
        new_y--;
        break;

    case 's':
    case 'S':
        new_y++;
        break;

    case 'a':
    case 'A':
        new_x--;
        break;

    case 'd':
    case 'D':
        new_x++;
        break;

    default:
        return;
    }


    // Don't allow coordinates outside the map.
    if (new_x < 0 ||
        new_y < 0 ||
        new_x >= (int)map_width ||
        new_y >= (int)map_height)
    {
        return;
    }


    // Check destination tile.
    char destination = map[new_y][new_x];


    // Only 'o' is considered as valid new tile for player FIX LATER
    if (destination != 'o')
    {
        return;
    }


    // Remove player from old position.
    map[player_data.pos_y][player_data.pos_x] = 'o';


    // Update player's position.
    player_data.pos_x = new_x;
    player_data.pos_y = new_y;


    // Draw player at new position.
    map[player_data.pos_y][player_data.pos_x] = 'P';

    // Reduce oxygen
    player_data.oxygen = player_data.oxygen - oxygen_loss;
}


/****************************************************************
 *
 * FUNCTION render_screen
 *
 ****************************************************************/
void render_screen(void)
{
    if (map == nullptr)
        return;


    cout << endl;


    for (size_t y = 0; y < map_height; y++)
    {
        for (size_t x = 0; x < map_width; x++)
        {
            cout << map[y][x] << ' '; //Make the rectangle even with ' '
        }

        cout << endl;
    }
    cout << endl;
    cout << "OXYGEN: " << player_data.oxygen << endl;
    cout << endl;
}


/****************************************************************
 *
 * FUNCTION start_splash_screen
 *
 ****************************************************************/
void start_splash_screen(void)
{
    cout << endl
        << "WELCOME to epic Holy Diver v0.01"
        << endl;

    cout << R"ASCII(              ,.  _~-.,               .
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

    cout << endl << "Enter commands and enjoy! "
        << "(press q to quit at all times)"
        << endl << endl;
}


/****************************************************************
 *
 * FUNCTION startup_routines
 *
 ****************************************************************/
void startup_routines(void)
{
    string filepath;
    int result;

    do
    {
        cout << "Enter path to TextMap file (0 = TextMap.txt): ";
        getline(cin, filepath);

        // Shortcut: pressing 0 loads the default map file.
        if (filepath == "0")
        {
            filepath = "TextMap.txt";
        }

        result = load_level(filepath);

        if (result < 0)
        {
            cout << "Please try again." << endl << endl;
        }

    } while (result < 0);

    current_Level_path = filepath;
}


/****************************************************************
 *
 * FUNCTION quit_routines
 *
 ****************************************************************/
void quit_routines(void)
{
    /************************************************************
     * Free dynamically allocated map memory.
     ************************************************************/

    free_map();


    cout << endl
        << "BYE! Welcome back soon."
        << endl;
}

void free_map(void)
{
    if (map != nullptr)
    {
        for (size_t y = 0; y < map_height; y++)
        {
            delete[] map[y];
        }

        delete[] map;
        map = nullptr;
    }

    map_width = 0;
    map_height = 0;
}