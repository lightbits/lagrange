#include "platform.h"
#include <cstdio>
// #define DEBUG
#define array_count(list) (sizeof((list))/sizeof((list)[0]))
#define IFKEYDOWN(KEY) if (input.key.down[SDL_SCANCODE_##KEY])
#define IFKEYUP(KEY) if (input.key.released[SDL_SCANCODE_##KEY])
#ifndef TWO_PI
#define TWO_PI 6.28318530718f
#endif
#define XRGB(HEX) (r32)(((HEX) >> 24) & 0xff) / 255.0f, \
                  (r32)(((HEX) >> 16) & 0xff) / 255.0f, \
                  (r32)(((HEX) >>  8) & 0xff) / 255.0f, \
                  (r32)(((HEX) >>  0) & 0xff) / 255.0f

struct Player
{
    vec2 position;
    vec2 Dposition;
    r32 theta;
    r32 Dtheta;

    r32 motor_constant;
    r32 l_motor;
    r32 r_motor;

    r32 mass;
    r32 arm;
    r32 inertia;
} player;

struct PlayerPendulumLink
{
    r32 k;
    r32 l0;
    r32 d;
} spring;

struct Pendulum
{
    vec2 position;
    vec2 Dposition;
    r32 mass;
    r32 radius;
} pendulum;

struct Roomba
{
    r32 x;
    r32 y;
    r32 dy0;
    r32 dy1;
    r32 dy2;
    r32 radius;
    r32 direction;
    r32 Rdirection;
    r32 turn_timer;
    r32 turn_timer0;
    r32 activate_timer;
    r32 activate_timer0;
    r32 speed;
} roomba;

struct Highscore
{
    int points;
    char nickname[256];
    char email[256];
} highscore;

struct HighscoreList
{
    int count;
    Highscore highscores[4096];
} highscore_list;

enum GameState
{
    GAME_PLAY,
    GAME_HIGHSCORE
};

#define NUM_PARTICLES 512
struct Particles
{
    int inactive[NUM_PARTICLES];
    int num_inactive;
    bool active[NUM_PARTICLES];
    vec2 position[NUM_PARTICLES];
    vec2 velocity[NUM_PARTICLES];
    r32 alpha[NUM_PARTICLES];
} particles;

struct Game
{
    GameState state;
} game;

enum TimerState
{
    TIMER_INACTIVE = 0,
    TIMER_BEGIN = 1,
    TIMER_ACTIVE = 2,
    TIMER_SUCCESS = 3,
    TIMER_ABORTED = 4
};

struct Timer
{
    TimerState state;
    r32 t;
    r32 duration;
    bool repeat;
};

#define TIMER_RED_LINE_CAPTURE timers[0]
#define TIMER_GREEN_LINE_CAPTURE timers[1]
#define TIMER_MAGNET timers[2]
#define TIMER_MAGNET_CELEBRATION timers[3]
#define TIMER_AUTOTURN timers[4]
#define TIMER_ROOMBA_LOSE timers[5]
#define TIMER_ROOMBA_WIN timers[6]
#define TIMER_PLAYER_TIME timers[7]
#define NUM_TIMERS 8
Timer timers[NUM_TIMERS];

#define ON_TIMER_SUCCESS(TIMER) if (TIMER.state == TIMER_SUCCESS)
#define ON_TIMER_ABORTED(TIMER) if (TIMER.state == TIMER_ABORTED)
#define ON_TIMER_BEGIN(TIMER) if (TIMER.state == TIMER_BEGIN)
#define DURING_TIMER(TIMER) if (TIMER.state == TIMER_ACTIVE || TIMER.state == TIMER_BEGIN)
#define TIMER_PROGRESS(TIMER) (1.0f-TIMER.t/TIMER.duration)
#define START_TIMER(TIMER) if (TIMER.state == TIMER_INACTIVE) { TIMER.state = TIMER_BEGIN; TIMER.t = TIMER.duration; }
#define ABORT_TIMER(TIMER) if (TIMER.state == TIMER_ACTIVE) TIMER.state = TIMER_ABORTED;

void init_timer(Timer *timer, r32 duration, bool repeat = false)
{
    timer->state = TIMER_INACTIVE;
    timer->t = 0.0f;
    timer->duration = duration;
    timer->repeat = repeat;
}

struct World
{
    r32 floor_level;
    r32 green_line;
    r32 red_line;
    r32 g;

    r32 right;
    r32 left;
    r32 top;
    r32 bottom;
} world;

r32 compute_hover_voltage()
{
    return sqrt(0.5f*(player.mass+pendulum.mass)*world.g/player.motor_constant);
}

void spawn_particle(vec2 p0, vec2 v0)
{
    if (particles.num_inactive > 0)
    {
        particles.num_inactive--;
        int index = particles.inactive[particles.num_inactive];
        particles.active[index] = true;
        particles.position[index] = p0;
        particles.velocity[index] = v0;
        particles.alpha[index] = 1.0f;
    }
}

void game_init()
{
    {
        particles.num_inactive = NUM_PARTICLES;
        for (int i = 0; i < NUM_PARTICLES; i++)
        {
            particles.inactive[i] = i;
            particles.active[i] = false;
        }
    }
    // load highscore list
    {
        FILE *file = fopen("gamedata.dat", "rb+");
        if (file)
        {
            size_t read_bytes = fread(&highscore_list, 1, sizeof(highscore_list), file);
            if (read_bytes != sizeof(highscore_list))
            {
                highscore_list.count = 0;
            }
            fclose(file);
        }
    }
    {
        highscore.points = 0;
        game.state = GAME_PLAY;
    }
    {
        init_timer(&TIMER_GREEN_LINE_CAPTURE, 2.0f);
        init_timer(&TIMER_RED_LINE_CAPTURE, 2.0f);
        init_timer(&TIMER_MAGNET, 0.45f);
        init_timer(&TIMER_MAGNET_CELEBRATION, 0.5f);
        init_timer(&TIMER_AUTOTURN, 8.5f, true);
        init_timer(&TIMER_ROOMBA_LOSE, 0.5f);
        init_timer(&TIMER_ROOMBA_WIN, 0.5f);
        init_timer(&TIMER_PLAYER_TIME, 60.0f);
        START_TIMER(TIMER_AUTOTURN);
        START_TIMER(TIMER_PLAYER_TIME);
    }
    {
        world.floor_level = 0.0f;
        world.green_line = 2.5f;
        world.red_line = -1.5f;
        world.g = 9.81f;
        world.right = +2.0f;
        world.left = -2.0f;
        world.top = +3.0f;
        world.bottom = -1.0f;
    }
    {
        spring.l0 = 0.8f;
        spring.k = 40.0f;
        spring.d = 1.0f;

        pendulum.mass = 0.05f;
        pendulum.radius = 0.1f;

        player.mass = 1.0f;
        player.arm = 0.5f;
        player.inertia = player.mass*player.arm*player.arm;

        player.motor_constant = 0.8f*(player.mass+pendulum.mass)*world.g;
        player.l_motor = compute_hover_voltage();
        player.r_motor = player.l_motor;

        roomba.radius = 0.5f;
        roomba.speed = 0.33f;
        roomba.Rdirection = -1.0f;
        roomba.y = world.floor_level+0.2f;
        roomba.dy0 = -0.1f;
        roomba.dy1 = +0.1f;
        roomba.dy2 = 0.4f;
    }
    {
        player.theta = 0.0f;
        player.Dtheta = 0.0f;
        player.position = m_vec2(0.0f, 2.0f);
        player.Dposition = m_vec2(0.0f, 0.0f);

        pendulum.position = m_vec2(player.position.x, player.position.y-spring.l0);
        pendulum.Dposition = m_vec2(0.0f, 0.0f);

        roomba.x = 0.0f;
        roomba.direction = -1.0f;
    }
}

r32 voltage_to_force_magnitude(r32 voltage)
{
    return player.motor_constant*voltage*voltage;
}

void glCircle(vec2 center, r32 radius, r32 t_max = TWO_PI, int n = 64)
{
    for (int i = 0; i < n; i++)
    {
        r32 t0 = TWO_PI * i / (r32)n;
        r32 t1 = TWO_PI * (i + 1) / (r32)n;
        bool should_break = false;
        if (t1 > t_max)
        {
            t1 = t_max;
            should_break = true;
        }
        r32 c0 = radius*cos(t0);
        r32 s0 = radius*sin(t0);
        r32 c1 = radius*cos(t1);
        r32 s1 = radius*sin(t1);
        glVertex2f(center.x, center.y);
        glVertex2f(center.x+c0, center.y+s0);
        glVertex2f(center.x+c1, center.y+s1);
        if (should_break)
            break;
    }
}

void glColor4x(u32 hex)
{
    glColor4f(XRGB(hex));
}

void glQuad(r32 x0, r32 y0, r32 x1, r32 y1)
{
    glVertex2f(x0, y0);
    glVertex2f(x1, y0);
    glVertex2f(x1, y1);
    glVertex2f(x1, y1);
    glVertex2f(x0, y1);
    glVertex2f(x0, y0);
}

void game_tick(Input input, VideoMode mode, r32 elapsed_time, r32 delta_time)
{
    // update timers
    {
        for (int i = 0; i < NUM_TIMERS; i++)
        {
            if (timers[i].state == TIMER_SUCCESS)
            {
                if (timers[i].repeat)
                {
                    timers[i].state = TIMER_BEGIN;
                }
                else
                {
                    timers[i].state = TIMER_INACTIVE;
                }
            }
            if (timers[i].state == TIMER_ABORTED)
            {
                timers[i].state = TIMER_INACTIVE;
            }
            if (timers[i].state == TIMER_BEGIN)
            {
                timers[i].t = timers[i].duration;
                timers[i].state = TIMER_ACTIVE;
            }
            if (timers[i].state == TIMER_ACTIVE)
            {
                timers[i].t -= delta_time;
                if (timers[i].t < 0.0f)
                {
                    timers[i].state = TIMER_SUCCESS;
                }
            }
        }
    }

    ON_TIMER_SUCCESS(TIMER_PLAYER_TIME)
    {
        game.state = GAME_HIGHSCORE;
        strcpy(highscore.nickname, "Nickname");
        strcpy(highscore.email, "YourEmail@ProbablyGmail.com");
    }

    // update game
    {
        // key input
        if (game.state == GAME_PLAY)
        {
            r32 hover_voltage = compute_hover_voltage();
            r32 dl = 0.0f;
            r32 dr = 0.0f;
            IFKEYDOWN(LEFT)
            {
                dl -= 0.05f;
                dr += 0.05f;
            }
            IFKEYDOWN(RIGHT)
            {
                dl += 0.05f;
                dr -= 0.05f;
            }
            IFKEYDOWN(UP)
            {
                dl += 0.05f;
                dr += 0.05f;
            }
            IFKEYDOWN(DOWN)
            {
                dl -= 0.05f;
                dr -= 0.05f;
            }
            player.l_motor = hover_voltage+dl;
            player.r_motor = hover_voltage+dr;
            if (player.l_motor > 1.0f) player.l_motor = 1.0f;
            if (player.l_motor < 0.0f) player.l_motor = 0.0f;
            if (player.r_motor > 1.0f) player.r_motor = 1.0f;
            if (player.r_motor < 0.0f) player.r_motor = 0.0f;
        }

        // spring force
        r32 spring_f = 0.0f;
        {
            r32 xa = player.position.x;
            r32 xb = pendulum.position.x;
            r32 Dxa = player.Dposition.x;
            r32 Dxb = pendulum.Dposition.x;
            r32 ya = player.position.y;
            r32 yb = pendulum.position.y;
            r32 Dya = player.Dposition.y;
            r32 Dyb = pendulum.Dposition.y;
            r32 l = sqrt((xa-xb)*(xa-xb) + (ya-yb)*(ya-yb));
            r32 Dl = ((xa-xb)*(Dxa-Dxb) + (ya-yb)*(Dya-Dyb)) / l;
            spring_f = spring.k*(l-spring.l0) + spring.d*Dl;
        }

        vec2 v_player_to_pendulum = pendulum.position-player.position;
        r32 distance = m_length(v_player_to_pendulum);
        if (distance > 0.01f)
            v_player_to_pendulum /= distance;
        vec2 v_pendulum_to_player = -v_player_to_pendulum;

        // update player
        {
            r32 dt = delta_time;
            vec2 tangent = m_vec2(cos(player.theta), sin(player.theta));
            vec2 normal = m_vec2(-tangent.y, tangent.x);
            r32 l_magnitude = voltage_to_force_magnitude(player.l_motor);
            r32 r_magnitude = voltage_to_force_magnitude(player.r_motor);
            vec2 l_force = l_magnitude*normal;
            vec2 r_force = r_magnitude*normal;

            if (player.position.y+player.arm*tangent.y < world.floor_level)
            {
                r_force.y += 1000.0f*(world.floor_level-player.position.y-player.arm*tangent.y);
            }
            if (player.position.y-player.arm*tangent.y < world.floor_level)
            {
                l_force.y += 1000.0f*(world.floor_level-player.position.y+player.arm*tangent.y);
            }

            vec2 s_force = spring_f*v_player_to_pendulum;
            vec2 g_force = m_vec2(0.0f, -player.mass*world.g);
            vec2 sum_forces = l_force+r_force+g_force+s_force;

            vec2 DDposition = sum_forces / player.mass;
            player.Dposition += DDposition * dt;
            player.position += player.Dposition * dt;

            r32 DDtheta = player.arm * (r_magnitude-l_magnitude) / player.inertia;
            player.Dtheta += DDtheta * dt;
            player.theta += player.Dtheta * dt;

            if (player.position.x > world.green_line+3.0f ||
                player.position.x < world.red_line-3.0f ||
                player.position.y < world.floor_level-2.0f ||
                player.position.y > 3.0f)
            {
                player.theta = 0.0f;
                player.Dtheta = 0.0f;
                player.position = m_vec2(0.0f, 2.0f);
                player.Dposition = m_vec2(0.0f, 0.0f);

                pendulum.position = m_vec2(player.position.x, player.position.y-spring.l0);
                pendulum.Dposition = m_vec2(0.0f, 0.0f);
            }
        }

        // update pendulum
        {
            r32 dt = delta_time;
            vec2 s_force = spring_f*v_pendulum_to_player;
            vec2 g_force = m_vec2(0.0f, -pendulum.mass*world.g);
            vec2 n_force = m_vec2(0.0f, 0.0f);

            // contact forces
            {
                r32 ay = pendulum.position.y-pendulum.radius;
                r32 by = world.floor_level;
                r32 cy = roomba.y+roomba.dy1;
                if (ay < by)
                {
                    n_force.y = 50.0f*(by-ay);
                }
                if (ay < cy && m_abs(pendulum.position.x-roomba.x) < roomba.radius)
                {
                    n_force.y = 50.0f*(cy-ay);
                }
            }
            vec2 delta_v = pendulum.Dposition - player.Dposition;
            vec2 f_force = -0.1f*delta_v*m_length(delta_v);
            vec2 sum_forces = g_force+s_force+f_force+n_force;

            vec2 DDposition = sum_forces / pendulum.mass;
            pendulum.Dposition += DDposition * dt;
            pendulum.position += pendulum.Dposition * dt;
        }

        // update camera
        {
            static vec2 position = m_vec2(0.0f, 0.0f);
            static vec2 Dposition = m_vec2(0.0f, 0.0f);
            r32 k = 1.0f;
            r32 d = 1.0f;
            vec2 reference = player.position;
            vec2 Dreference = player.Dposition;
            if (player.position.x > 0.3f*world.green_line)
            {
                reference.x = 0.3f*world.green_line;
                Dreference.x = 0.0f;
            }
            if (player.position.x < 0.3f*world.red_line)
            {
                reference.x = 0.3f*world.red_line;
                Dreference.x = 0.0f;
            }
            vec2 e = reference-position;
            vec2 De = Dreference-Dposition;
            vec2 DDposition = k*e + d*De;

            Dposition += DDposition*delta_time;
            position += Dposition*delta_time;
            r32 radius = 3.0f;

            world.right = (mode.width / (r32)mode.height)*(position.x+radius);
            world.left = (mode.width / (r32)mode.height)*(position.x-radius);
            world.top = position.y+radius;
            world.bottom = position.y-radius;
        }

        // update roomba
        {
            roomba.x += roomba.direction*roomba.speed*delta_time;
            ON_TIMER_SUCCESS(TIMER_AUTOTURN)
            {
                roomba.Rdirection *= -1.0f;
            }
            roomba.direction += 5.0f*(roomba.Rdirection-roomba.direction)*delta_time;

            if (m_abs(pendulum.position.x-roomba.x) < roomba.radius &&
                pendulum.position.y > roomba.y+roomba.dy1 &&
                pendulum.position.y-pendulum.radius < roomba.y+roomba.dy2)
            {
                if (TIMER_MAGNET_CELEBRATION.state != TIMER_ACTIVE)
                {
                    START_TIMER(TIMER_MAGNET);
                }
            }
            else
            {
                ABORT_TIMER(TIMER_MAGNET);
            }

            ON_TIMER_SUCCESS(TIMER_MAGNET)
            {
                START_TIMER(TIMER_MAGNET_CELEBRATION);
                roomba.Rdirection *= -1.0f;
            }

            // Red field
            {
                if (roomba.x - roomba.radius < world.red_line &&
                    TIMER_ROOMBA_LOSE.state != TIMER_ACTIVE)
                {
                    START_TIMER(TIMER_RED_LINE_CAPTURE);
                }
                else
                {
                    ABORT_TIMER(TIMER_RED_LINE_CAPTURE);
                }

                ON_TIMER_SUCCESS(TIMER_RED_LINE_CAPTURE)
                {
                    START_TIMER(TIMER_ROOMBA_LOSE);
                    if (game.state == GAME_PLAY)
                        highscore.points--;
                }

                DURING_TIMER(TIMER_ROOMBA_LOSE)
                {
                    static r32 x0 = 0.0f;
                    ON_TIMER_BEGIN(TIMER_ROOMBA_LOSE)
                    {
                        x0 = roomba.x;
                    }
                    r32 t = TIMER_PROGRESS(TIMER_ROOMBA_LOSE);
                    roomba.x = x0 - 32.0f*m_smoothstep(0.0f, 1.0f, t);
                }

                ON_TIMER_SUCCESS(TIMER_ROOMBA_LOSE)
                {
                    roomba.x = 0.0f;
                }
            }

            // Green field
            {
                // IFKEYDOWN(Y)
                if (roomba.x + roomba.radius > world.green_line &&
                    TIMER_ROOMBA_WIN.state != TIMER_ACTIVE)
                {
                    START_TIMER(TIMER_GREEN_LINE_CAPTURE);
                }
                else
                {
                    ABORT_TIMER(TIMER_GREEN_LINE_CAPTURE);
                }

                ON_TIMER_SUCCESS(TIMER_GREEN_LINE_CAPTURE)
                {
                    START_TIMER(TIMER_ROOMBA_WIN);
                    if (game.state == GAME_PLAY)
                        highscore.points++;
                }

                DURING_TIMER(TIMER_ROOMBA_WIN)
                {
                    static r32 x0 = 0.0f;
                    ON_TIMER_BEGIN(TIMER_ROOMBA_WIN)
                    {
                        x0 = roomba.x;
                    }
                    r32 t = TIMER_PROGRESS(TIMER_ROOMBA_WIN);
                    roomba.x = x0 + 32.0f*m_smoothstep(0.0f, 1.0f, t);
                }

                ON_TIMER_SUCCESS(TIMER_ROOMBA_WIN)
                {
                    roomba.x = 0.0f;
                }
            }
        }
    }
    // end update

    // spawn particles
    #ifdef PARTICLES
    {
        vec2 tangent = m_vec2(cos(player.theta), sin(player.theta));
        vec2 normal = m_vec2(-tangent.y, tangent.x);
        vec2 right_wing = player.position + 0.8f*player.arm*tangent;
        vec2 left_wing = player.position - 0.8f*player.arm*tangent;

        bool left = false;
        bool right = false;
        IFKEYDOWN(RIGHT) right = true;
        IFKEYDOWN(LEFT) left = true;
        IFKEYDOWN(UP) { right = true; left = true; }
        if (left)
        {
            r32 v1 = 0.3f+0.3f*frand();
            r32 v2 = -0.3f+0.6f*frand();
            spawn_particle(left_wing, -v1*normal+v2*tangent);
        }
        if (right)
        {
            r32 v1 = 0.3f+0.3f*frand();
            r32 v2 = -0.3f+0.6f*frand();
            spawn_particle(right_wing, -v1*normal+v2*tangent);
        }
    }
    #endif

    // update particles
    #ifdef PARTICLES
    {
        for (int i = 0; i < NUM_PARTICLES; i++)
        {
            bool active = particles.active[i];
            if (active)
            {
                r32 alpha = particles.alpha[i];
                vec2 p = particles.position[i];
                vec2 v = particles.velocity[i];
                v.y -= 0.1f*world.g*delta_time;
                p += v*delta_time;
                if (p.y < world.floor_level)
                {
                    p.y = world.floor_level;
                    v.y *= -0.95f;
                }
                alpha -= delta_time;

                if (alpha < 0.0f)
                {
                    particles.active[i] = false;
                    particles.inactive[particles.num_inactive] = i;
                    particles.num_inactive++;
                }

                particles.position[i] = p;
                particles.velocity[i] = v;
                particles.alpha[i] = alpha;
            }
        }
    }
    #endif

    glViewport(0, 0, mode.width, mode.height);
    glClearColor(XRGB(0xE2D7B5FF));
    glClear(GL_COLOR_BUFFER_BIT);

    // render world
    {
        // camera projection matrix
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        {
            r32 Ax = 2.0f / (world.right-world.left);
            r32 Bx = 1.0f - Ax*world.right;
            r32 Ay = 2.0f / (world.top-world.bottom);
            r32 By = 1.0f - Ay*world.top;
            r32 data[] = {
                Ax,   0.0f, 0.0f, 0.0f,
                0.0f, Ay,   0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                Bx,   By,   0.0f, 1.0f
            };
            glLoadMatrixf(data);
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // draw floor
        {
            glLineWidth(4.0f);
            glBegin(GL_TRIANGLES);
            glColor4f(XRGB(0xE2D7B5FF));
            glVertex2f(world.left, world.floor_level);
            glVertex2f(world.left, world.floor_level-5.0f);
            glVertex2f(world.right, world.floor_level-5.0f);
            glVertex2f(world.right, world.floor_level-5.0f);
            glVertex2f(world.right, world.floor_level);
            glVertex2f(world.left, world.floor_level);

            glColor4f(XRGB(0x6AB417FF));
            glVertex2f(world.green_line, world.floor_level);
            glVertex2f(world.green_line, world.floor_level-0.1f);
            glVertex2f(world.green_line+2.0f, world.floor_level-0.1f);
            glVertex2f(world.green_line+2.0f, world.floor_level-0.1f);
            glVertex2f(world.green_line+2.0f, world.floor_level);
            glVertex2f(world.green_line, world.floor_level);

            glColor4f(XRGB(0xE03C28FF));
            glVertex2f(world.red_line, world.floor_level);
            glVertex2f(world.red_line, world.floor_level-0.1f);
            glVertex2f(world.red_line-2.0f, world.floor_level-0.1f);
            glVertex2f(world.red_line-2.0f, world.floor_level-0.1f);
            glVertex2f(world.red_line-2.0f, world.floor_level);
            glVertex2f(world.red_line, world.floor_level);
            glEnd();
        }

        // draw player
        {
            vec2 tangent = m_vec2(cos(player.theta), sin(player.theta));
            vec2 center = player.position;
            vec2 right_wing = center + tangent*player.arm;
            vec2 left_wing = center - tangent*player.arm;
            glBegin(GL_LINES);
            glColor4f(XRGB(0x1A1A1AFF));
            glVertex2f(left_wing.x, left_wing.y);
            glVertex2f(right_wing.x, right_wing.y);

            glColor4f(XRGB(0x00000055));
            glVertex2f(m_min(left_wing.x, pendulum.position.x), world.floor_level);
            glVertex2f(m_max(right_wing.x, pendulum.position.x), world.floor_level);
            glEnd();
        }

        // draw pendulum
        {
            vec2 a = player.position;
            vec2 b = pendulum.position;
            glBegin(GL_LINES);
            glColor4f(XRGB(0x1A1A1AFF));
            glVertex2f(a.x, a.y);
            glVertex2f(b.x, b.y);
            glEnd();

            glBegin(GL_TRIANGLES);
            glColor4f(XRGB(0x1A1A1AFF));
            glCircle(pendulum.position, pendulum.radius);
            glEnd();
        }

        // draw roomba
        {
            glBegin(GL_TRIANGLES);
            {
                r32 x0 = roomba.x-roomba.radius;
                r32 x1 = roomba.x+roomba.radius;
                r32 y0 = roomba.y+roomba.dy0;
                r32 y1 = roomba.y+roomba.dy1;
                glColor4f(XRGB(0x1A1A1AFF));
                glVertex2f(x0, y0);
                glVertex2f(x1, y0);
                glVertex2f(x1, y1);
                glVertex2f(x1, y1);
                glVertex2f(x0, y1);
                glVertex2f(x0, y0);
            }
            {
                r32 x0 = roomba.x-0.8f*roomba.radius;
                r32 x1 = roomba.x+0.8f*roomba.radius;
                r32 y0 = roomba.y+roomba.dy1;
                r32 y1 = roomba.y+roomba.dy2;
                glColor4f(XRGB(0xE03C2877)); glVertex2f(x0, y0);
                glColor4f(XRGB(0xE03C2877)); glVertex2f(x1, y0);
                glColor4f(XRGB(0xE03C2822)); glVertex2f(x1, y1);
                glColor4f(XRGB(0xE03C2822)); glVertex2f(x1, y1);
                glColor4f(XRGB(0xE03C2822)); glVertex2f(x0, y1);
                glColor4f(XRGB(0xE03C2877)); glVertex2f(x0, y0);
            }
            glEnd();

            r32 inner_eye = 0.1f;
            r32 outer_eye = 0.7f;
            r32 eye_radius = 0.2f;

            glBegin(GL_LINES);
            glColor4f(XRGB(0xE2D7B5FF));
            {
                // left eye
                r32 dx = -outer_eye+(-inner_eye+outer_eye)*(0.5f+0.5f*roomba.direction);
                r32 cx = roomba.x+roomba.radius*dx;
                r32 x0 = cx-eye_radius*roomba.radius;
                r32 x1 = cx+eye_radius*roomba.radius;
                r32 y = roomba.y;
                glVertex2f(x0, y);
                glVertex2f(x1, y);
            }
            {
                // right eye
                r32 dx = inner_eye+(outer_eye-inner_eye)*(0.5f+0.5f*roomba.direction);
                r32 cx = roomba.x+roomba.radius*dx;
                r32 x0 = cx-eye_radius*roomba.radius;
                r32 x1 = cx+eye_radius*roomba.radius;
                glVertex2f(x0, roomba.y);
                glVertex2f(x1, roomba.y);
            }
            glColor4f(XRGB(0x00000055));
            {
                glVertex2f(roomba.x-roomba.radius, world.floor_level);
                glVertex2f(roomba.x+roomba.radius, world.floor_level);
            }
            glEnd();
        }

        // draw particles
        #ifdef PARTICLES
        {
            glBegin(GL_TRIANGLES);
            for (int i = 0; i < NUM_PARTICLES; i++)
            {
                if (particles.active[i])
                {
                    vec2 p = particles.position[i];
                    r32 alpha = particles.alpha[i];
                    glColor4f(0.0f, 0.0f, 0.0f, 0.5f*alpha);
                    glCircle(p, 0.02f, TWO_PI, 16);
                }
            }
            glEnd();
        }
        #endif

        // draw magnet timer
        DURING_TIMER(TIMER_MAGNET)
        {
            glBegin(GL_TRIANGLES);
            glColor4f(XRGB(0xE03C28FF));
            glCircle(m_vec2(roomba.x, roomba.y+roomba.dy1+0.5f),
                        0.3f,
                        TWO_PI*TIMER_PROGRESS(TIMER_MAGNET));
            glEnd();
        }

        DURING_TIMER(TIMER_RED_LINE_CAPTURE)
        {
            glBegin(GL_TRIANGLES);
            glColor4f(XRGB(0xE03C28FF));
            r32 arc = TWO_PI*TIMER_PROGRESS(TIMER_RED_LINE_CAPTURE);
            vec2 center = m_vec2(world.red_line-1.0f, world.floor_level-0.5f);
            glCircle(center, 0.3f, arc);
            glEnd();
        }

        DURING_TIMER(TIMER_GREEN_LINE_CAPTURE)
        {
            glBegin(GL_TRIANGLES);
            glColor4f(XRGB(0x6AB417FF));
            r32 arc = TWO_PI*TIMER_PROGRESS(TIMER_GREEN_LINE_CAPTURE);
            vec2 center = m_vec2(world.green_line+1.0f, world.floor_level-0.5f);
            glCircle(center, 0.3f, arc);
            glEnd();
        }

        DURING_TIMER(TIMER_MAGNET_CELEBRATION)
        {
            r32 t = TIMER_PROGRESS(TIMER_MAGNET_CELEBRATION);
            r32 t0 = 2.0f*(t+0.1f)*(t+0.1f)*(t+0.1f);
            r32 t1 = 0.2f+1.9f*t*t;
            if (t0 > t1)
                t0 = t1;
            vec2 c = m_vec2(roomba.x, roomba.y+roomba.dy1+0.5f);
            // TODO: Random thetas
            static r32 thetas[] = {
                0.1f, 0.7f, 1.4f, 1.6f,
                2.6f, 3.5f, 4.5f, 5.5f
            };
            glBegin(GL_LINES);
            for (int i = 0; i < 8; i++)
            {
                r32 theta = thetas[i];
                r32 cost = cos(theta);
                r32 sint = sin(theta);
                glColor4f(XRGB(0x000000FF)); glVertex2f(c.x+t0*cost, c.y+t0*sint);
                glColor4f(XRGB(0x000000FF)); glVertex2f(c.x+t1*cost, c.y+t1*sint);
            }
            glEnd();
        }

        // TODO: better win anim
        DURING_TIMER(TIMER_ROOMBA_WIN)
        {
            TIMER_PLAYER_TIME.t += 16.0f*delta_time;

            static vec2 center = m_vec2(0.0f, 0.0f);
            ON_TIMER_BEGIN(TIMER_ROOMBA_WIN)
            {
                center = m_vec2(roomba.x, (roomba.y+roomba.dy0+roomba.y+roomba.dy1)/2.0f);
            }
            r32 t = TIMER_PROGRESS(TIMER_ROOMBA_WIN);
            r32 t0 = 2.0f*(t+0.1f)*(t+0.1f)*(t+0.1f);
            r32 t1 = 0.2f+1.9f*t*t;
            if (t0 > t1)
                t0 = t1;
            static r32 thetas[] = {
                0.1f, 0.7f, 1.4f, 1.6f,
                2.6f, 3.5f, 4.5f, 5.5f
            };
            glBegin(GL_LINES);
            for (int i = 0; i < 8; i++)
            {
                r32 theta = thetas[i];
                r32 cost = cos(theta);
                r32 sint = sin(theta);
                glColor4f(XRGB(0x000000FF)); glVertex2f(center.x+t0*cost, center.y+t0*sint);
                glColor4f(XRGB(0x000000FF)); glVertex2f(center.x+t1*cost, center.y+t1*sint);
            }
            glEnd();
        }

        // TODO: better lose anim
        DURING_TIMER(TIMER_ROOMBA_LOSE)
        {
            static vec2 center = m_vec2(0.0f, 0.0f);
            ON_TIMER_BEGIN(TIMER_ROOMBA_LOSE)
            {
                center = m_vec2(roomba.x, (roomba.y+roomba.dy0+roomba.y+roomba.dy1)/2.0f);
            }
            r32 t = TIMER_PROGRESS(TIMER_ROOMBA_LOSE);
            r32 t0 = 2.0f*(t+0.1f)*(t+0.1f)*(t+0.1f);
            r32 t1 = 0.2f+1.9f*t*t;
            if (t0 > t1)
                t0 = t1;
            static r32 thetas[] = {
                0.1f, 0.7f, 1.4f, 1.6f,
                2.6f, 3.5f, 4.5f, 5.5f
            };
            glBegin(GL_LINES);
            for (int i = 0; i < 8; i++)
            {
                r32 theta = thetas[i];
                r32 cost = cos(theta);
                r32 sint = sin(theta);
                glColor4f(XRGB(0x000000FF)); glVertex2f(center.x+t0*cost, center.y+t0*sint);
                glColor4f(XRGB(0x000000FF)); glVertex2f(center.x+t1*cost, center.y+t1*sint);
            }
            glEnd();
        }
    }

    // render gui
    {
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();

        DURING_TIMER(TIMER_PLAYER_TIME)
        {
            glBegin(GL_TRIANGLES);
            glColor4f(XRGB(0xE03C28FF));
            r32 x0 = -1.0f;
            r32 x1 = 1.0f-2.0f*TIMER_PROGRESS(TIMER_PLAYER_TIME);
            glVertex2f(x0, +0.95f);
            glVertex2f(x1, +0.95f);
            glVertex2f(x1, +1.00f);
            glVertex2f(x1, +1.00f);
            glVertex2f(x0, +1.00f);
            glVertex2f(x0, +0.95f);
            glEnd();
        }

        #ifdef DEBUG
        {
            using namespace ImGui;
            if (Button("Increase"))
            {
                highscore.points++;
            }
            if (Button("Decrease"))
            {
                highscore.points--;
            }
            if (Button("Reset"))
            {
                game_init();
            }
            Text("Highscore: %d", highscore.points);
            Text("Particles: %d\n", particles.num_inactive);
        }
        #endif


        // highscore screen
        if (game.state == GAME_HIGHSCORE)
        {
            glBegin(GL_TRIANGLES);
            glColor4x(0xE2D7B555);
            glQuad(-1.0f, -1.0f, +1.0f, +1.0f);
            glEnd();
            using namespace ImGui;
            PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f);
            PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16.0f, 16.0f));
            PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            SetNextWindowPosCenter();
            SetNextWindowSize(ImVec2(500.0f, 265.0f));
            Begin("Enter your details!", NULL,
                  ImGuiWindowFlags_NoTitleBar|
                  ImGuiWindowFlags_NoResize);
            PushItemWidth(450.0f);
            InputText("##Nickname", highscore.nickname, sizeof(highscore.nickname));
            InputText("##E-mail", highscore.email, sizeof(highscore.email));
            PopItemWidth();
            if (Button("Save and try again"))
            {
                if (highscore_list.count < array_count(highscore_list.highscores))
                {
                    highscore_list.highscores[highscore_list.count] = highscore;
                    highscore_list.count++;
                }
                {
                    FILE *file = fopen("gamedata.dat", "wb+");
                    if (file)
                    {
                        fwrite(&highscore_list, 1, sizeof(highscore_list), file);
                        fclose(file);
                    }
                }
                {
                    FILE *file = fopen("highscores.txt", "w+");
                    if (file)
                    {
                        for (int i = 0; i < highscore_list.count; i++)
                        {
                            Highscore h = highscore_list.highscores[i];
                            fprintf(file, "%d points. %s (%s)\n", h.points, h.nickname, h.email);
                        }
                        fclose(file);
                    }
                }
                game_init();
            }
            SameLine();
            if (Button("Try again"))
            {
                game_init();
            }
            End();
            PopStyleVar();
            PopStyleVar();
            PopStyleVar();
        }

        // draw histogram
        {
            int bins[8];
            int max_count = 0;
            for (int i = 0; i < array_count(bins); i++)
            {
                bins[i] = 0;
            }
            for (int i = 0; i < highscore_list.count; i++)
            {
                int points = highscore_list.highscores[i].points;
                int bin = points+array_count(bins)/2;
                if (bin < 0) bin = 0;
                if (bin > array_count(bins)-1) bin = array_count(bins)-1;
                bins[bin]++;
                if (bins[bin] > max_count)
                    max_count = bins[bin];
            }
            glBegin(GL_TRIANGLES);
            r32 w = 0.8f;
            r32 wi = 0.2f * w / array_count(bins);
            for (int i = 0; i < array_count(bins); i++)
            {
                int count = bins[i];
                r32 x = -w/2.0f + w*i/(r32)array_count(bins);
                r32 x0 = x-0.5f*wi;
                r32 x1 = x+0.5f*wi;
                r32 y0 = 0.5f;
                r32 y1 = 0.5f+0.4f*count/(r32)max_count;
                if (game.state == GAME_PLAY)
                    glColor4x(0x1a1a1a22);
                else
                    glColor4x(0xAB6666ff);
                glQuad(x0, y0, x1, y1);
            }
            {
                int my_bin = highscore.points+array_count(bins)/2;
                if (my_bin < 0) my_bin = 0;
                if (my_bin > array_count(bins)-1) my_bin = array_count(bins)-1;
                r32 x = -w/2.0f + w*my_bin/(r32)array_count(bins);
                if (game.state == GAME_PLAY)
                    glColor4x(0x1a1a1a22);
                else
                    glColor4x(0xAB6666ff);
                glVertex2f(x-wi, 0.40f);
                glVertex2f(x+wi, 0.40f);
                glVertex2f(x, 0.45f);
            }
            glEnd();
        }
    }
    // end render
}

#include "platform_sdl.cpp"
