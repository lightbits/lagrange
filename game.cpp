/* TODO:
* Highscore tracking
* Highscore display
* Store email and name
* Victory polish
*/

#include "platform.h"
#include <cstdio>
#define IFKEYDOWN(KEY) if (input.key.down[SDL_SCANCODE_##KEY])
#define IFKEYUP(KEY) if (input.key.released[SDL_SCANCODE_##KEY])
#ifndef TWO_PI
#define TWO_PI 6.28318530718f
#endif
#define XRGB(HEX) (r32)(((HEX) >> 24) & 0xff) / 255.0f, \
                  (r32)(((HEX) >> 16) & 0xff) / 255.0f, \
                  (r32)(((HEX) >>  8) & 0xff) / 255.0f, \
                  (r32)(((HEX) >>  0) & 0xff) / 255.0f

struct Font
{
    GLuint texture;
    stbtt_bakedchar cdata[96]; // ASCII 32..126 is 95 glyphs
} font;

void pxPrint(r32 x, r32 y, char *text)
{
    // assume orthographic projection with units = screen pixels, origin at top left
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font.texture);
    glBegin(GL_QUADS);
    while (*text) {
      if (*text >= 32 && *text < 128) {
         stbtt_aligned_quad q;
         stbtt_GetBakedQuad(font.cdata, 512,512, *text-32, &x,&y,&q,1);
         glColor4f(0.0f, 0.0f, 0.0f, 1.0f);
         glTexCoord2f(q.s0,q.t0); glVertex2f(q.x0,q.y0);
         glTexCoord2f(q.s1,q.t0); glVertex2f(q.x1,q.y0);
         glTexCoord2f(q.s1,q.t1); glVertex2f(q.x1,q.y1);
         glTexCoord2f(q.s0,q.t1); glVertex2f(q.x0,q.y1);
      }
      ++text;
    }
    glEnd();
    glDisable(GL_TEXTURE_2D);
}

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
} highscore;

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
#define NUM_TIMERS 7
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

    // camera
    r32 right;
    r32 left;
    r32 top;
    r32 bottom;
} world;

r32 compute_hover_voltage()
{
    return sqrt(0.5f*(player.mass+pendulum.mass)*world.g/player.motor_constant);
}

void game_init()
{
    {
        highscore.points = 0;
    }
    {
        init_timer(&TIMER_GREEN_LINE_CAPTURE, 2.0f);
        init_timer(&TIMER_RED_LINE_CAPTURE, 2.0f);
        init_timer(&TIMER_MAGNET, 0.3f);
        init_timer(&TIMER_MAGNET_CELEBRATION, 0.5f);
        init_timer(&TIMER_AUTOTURN, 6.0f, true);
        init_timer(&TIMER_ROOMBA_LOSE, 0.5f);
        init_timer(&TIMER_ROOMBA_WIN, 0.5f);
        START_TIMER(TIMER_AUTOTURN);
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
    #if 0
    {
        u08 *ttf_buffer = so_read_file_and_null_terminate("C:/Windows/Fonts/Arial.ttf");
        u08 bitmap_buffer[512*512];
        if (ttf_buffer)
        {
            stbtt_BakeFontBitmap(ttf_buffer,0, 64.0,bitmap_buffer,512,512, 32,96, font.cdata); // no guarantee this fits!
            // can free ttf_buffer at this point
            glGenTextures(1, &font.texture);
            glBindTexture(GL_TEXTURE_2D, font.texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 512,512, 0, GL_ALPHA, GL_UNSIGNED_BYTE, bitmap_buffer);
            // can free temp_bitmap at this point
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        }
        so_free(ttf_buffer);
    }
    #endif
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

void game_tick(Input input, VideoMode mode, r32 elapsed_time, r32 delta_time)
{
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

    // update
    {
        // key input
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
            r32 k = 2.0f;
            r32 d = 2.0f;
            vec2 reference = player.position;
            vec2 Dreference = player.Dposition;
            if (player.position.x > world.green_line)
            {
                reference.x = 0.8f*world.green_line;
                Dreference.x = 0.0f;
            }
            if (player.position.x < world.red_line)
            {
                reference.x = 0.8f*world.red_line;
                Dreference.x = 0.0f;
            }
            vec2 e = reference-position;
            vec2 De = Dreference-Dposition;
            vec2 DDposition = k*e + d*De;

            Dposition += DDposition*delta_time;
            position += Dposition*delta_time;
            // static r32 radius = 2.0f;
            // static r32 Dradius = 0.0f;
            // r32 Rradius = 2.0f;
            // {
            //     r32 distance = m_length(pendulum.position-m_vec2(roomba.x, roomba.y+roomba.dy1));
            //     r32 distance0 = 0.05f;
            //     r32 distance1 = 2.0f;
            //     Rradius = 1.8f + (2.2f-1.8f)*(distance-distance0)/(distance1-distance0);
            // }

            // {
            //     r32 DDradius = 10.0f*(Rradius-radius)-2.0f*Dradius;
            //     Dradius += DDradius*delta_time;
            //     radius += Dradius*delta_time;
            // }
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
                pendulum.position.y-pendulum.radius > roomba.y+roomba.dy1 &&
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

        #if 0
        {
            glMatrixMode(GL_PROJECTION);
            glLoadIdentity();
            {
                r32 Ax = +2.0f / mode.width;
                r32 Bx = -1.0f;
                r32 Ay = -2.0f / mode.height;
                r32 By = +1.0f;
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
            pxPrint(100.0f, 200.0f, "You!");
            glDisable(GL_BLEND);
        }
        #endif

        {
            using namespace ImGui;
            if (Button("Reset"))
            {
                game_init();
            }
            Text("Highscore: %d", highscore.points);
        }
    }
    // end render
}

#include "platform_sdl.cpp"
