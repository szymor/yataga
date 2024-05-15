#include <stdio.h>

#include <SDL/SDL.h>
#include <SDL/SDL_gfxPrimitives.h>

#include <chipmunk.h>

#define SCREEN_WIDTH	(800)
#define SCREEN_HEIGHT	(600)
#define TIME_STEP		(15)
#define SHOOT_RELOAD	(100)

enum MetaType
{
	MT_NULL,
	MT_TANK,
	MT_BULLET,
	MT_SCRAP,
	MT_END
};

enum ControlFlags
{
	CF_FORWARD = 1,
	CF_REVERSE = 2,
	CF_TURN_LEFT = 4,
	CF_TURN_RIGHT = 8,
	CF_SHOOT = 16,
	CF_GRIP = 32
};

enum Team
{
	TEAM_NEUTRAL = 1,
	TEAM_PLAYER = 2,
	TEAM_ALLY = 4,
	TEAM_ENEMY = 8,
	TEAM_SCRAP = 16
};

struct Metadata
{
	enum MetaType type;
	unsigned int control_flags;
	enum Team team;
	int shoot_timer;
	int scrap_timer;
};

SDL_Surface *screen = NULL;

cpSpace *world_space = NULL;
cpBody *player = NULL;

unsigned int game_timer = 0;
unsigned int gameover_time = 0;

void world_init(void);
void world_free(void);
cpBody* world_spawn_body(cpFloat x, cpFloat y, enum MetaType mt, enum Team team);
void world_kill_body(cpBody *body);
void cbKillBody(cpSpace *space, void *obj, void *data);
void world_process(int ms);
void world_draw(cpVect *cc);

void tank_shoot(cpBody *body);
void cbTankShoot(cpSpace *space, void *obj, void *data);

void cbBodyDraw(cpBody *body, void *data);
void cbBodyFree(cpBody *body, void *data);
void cbShapeDraw(cpBody *body, cpShape *shape, void *data);
void cbShapeFree(cpBody *body, cpShape *shape, void *data);
void cbScrap(cpBody *body, cpShape *shape, void *data);

void cbOnPlayerEnemyTouch(cpArbiter *arb, cpSpace *space, cpDataPointer data);

int main(int argc, char *argv[])
{
	// init
	SDL_Init(SDL_INIT_VIDEO);
	screen = SDL_SetVideoMode(SCREEN_WIDTH, SCREEN_HEIGHT, 16, SDL_HWSURFACE);
	SDL_WM_SetCaption("Yet Another Tank Game", NULL);
	SDL_ShowCursor(SDL_DISABLE);

	world_init();
	player = world_spawn_body(0, 0, MT_TANK, TEAM_PLAYER);
	struct Metadata *md = cpBodyGetUserData(player);

	world_spawn_body(100, 100, MT_TANK, TEAM_ALLY);
	world_spawn_body(100, -100, MT_TANK, TEAM_ALLY);
	world_spawn_body(-100, -100, MT_TANK, TEAM_ALLY);
	world_spawn_body(-100, 100, MT_TANK, TEAM_ALLY);
	for (int i = 0; i < 4; ++i)
	{
		world_spawn_body(-100 + i * 20, 200, MT_TANK, TEAM_ENEMY);
		//world_spawn_body(i * 20, -300, MT_TANK, TEAM_ENEMY);
		//world_spawn_body(100 + i * 20, -100, MT_TANK, TEAM_ALLY);
	}

	// loop
	int quit = 0;
	SDL_Event event;
	while (!quit)
	{
		if (SDL_PollEvent(&event))
		{
			// input
			switch (event.type)
			{
				case SDL_KEYDOWN:
					switch (event.key.keysym.sym)
					{
						case SDLK_UP:
							if (0 == gameover_time)
								md->control_flags |= CF_FORWARD;
							break;
						case SDLK_DOWN:
							if (0 == gameover_time)
								md->control_flags |= CF_REVERSE;
							break;
						case SDLK_LEFT:
							if (0 == gameover_time)
								md->control_flags |= CF_TURN_LEFT;
							break;
						case SDLK_RIGHT:
							if (0 == gameover_time)
								md->control_flags |= CF_TURN_RIGHT;
							break;
						case SDLK_LCTRL:
							if (0 == gameover_time)
							{
								md->control_flags |= CF_SHOOT;
								md->shoot_timer = 0;
							}
							break;
						case SDLK_SPACE:
							if (0 == gameover_time)
								md->control_flags |= CF_GRIP;
							break;
						case SDLK_ESCAPE:
							quit = 1;
							break;
					}
					break;
				case SDL_KEYUP:
					switch (event.key.keysym.sym)
					{
						case SDLK_UP:
							md->control_flags &= ~CF_FORWARD;
							break;
						case SDLK_DOWN:
							md->control_flags &= ~CF_REVERSE;
							break;
						case SDLK_LEFT:
							md->control_flags &= ~CF_TURN_LEFT;
							break;
						case SDLK_RIGHT:
							md->control_flags &= ~CF_TURN_RIGHT;
							break;
						case SDLK_LCTRL:
							md->control_flags &= ~CF_SHOOT;
							break;
						case SDLK_SPACE:
							md->control_flags &= ~CF_GRIP;
							break;
					}
					break;
				case SDL_QUIT:
					quit = 1;
					break;
			}
		}
		else
		{
			// draw
			cpVect pos = cpBodyGetPosition(player);
			world_draw(&pos);

			char buff[64];

			if (gameover_time)
			{
				if (gameover_time >= 60000)
				{
					sprintf(buff, "You lost after %dm %ds", gameover_time / 60000, (gameover_time % 60000) / 1000);
				}
				else
				{
					sprintf(buff, "You lost after %ds", gameover_time / 1000);
				}
				stringColor(screen, 0, 0, buff, 0x000000ff);

				sprintf(buff, "GAME OVER");
				stringColor(screen, 0, 8, buff, 0x000000ff);
			}
			else
			{
				if (game_timer >= 60000)
				{
					sprintf(buff, "Time: %dm %ds", game_timer / 60000, (game_timer % 60000) / 1000);
				}
				else
				{
					sprintf(buff, "Time: %ds", game_timer / 1000);
				}
				stringColor(screen, 0, 0, buff, 0x000000ff);
			}

			SDL_Flip(screen);

			// process
			world_process(TIME_STEP);
			SDL_Delay(TIME_STEP);
			game_timer += TIME_STEP;
		}
	}

	world_free();
	SDL_Quit();
	return 0;
}

void world_init(void)
{
	world_space = cpSpaceNew();
	cpSpaceSetDamping(world_space, 0.7);

	cpCollisionHandler *ch = NULL;
	ch = cpSpaceAddCollisionHandler(world_space, TEAM_PLAYER, TEAM_ENEMY);
	ch->postSolveFunc = cbOnPlayerEnemyTouch;
}

void world_free(void)
{
	// to be fixed, valgrind complains
	cpSpaceEachBody(world_space, cbBodyFree, NULL);
	cpSpaceFree(world_space);
}

cpBody* world_spawn_body(cpFloat x, cpFloat y, enum MetaType mt, enum Team team)
{
	cpFloat radius = 1;
	cpFloat mass = 1;
	if (mt == MT_TANK)
	{
		radius = team == TEAM_PLAYER ? 10 : 7;
		mass = team == TEAM_PLAYER ? 100 : 50;
	}
	else if (mt == MT_BULLET)
	{
		radius = 2;
		mass = 30;
	}

	cpFloat moment = cpMomentForCircle(mass, 0, radius, cpvzero);

	cpBody *body = cpSpaceAddBody(world_space, cpBodyNew(mass, moment));
	cpBodySetPosition(body, cpv(x, y));

	struct Metadata *metadata = malloc(sizeof(struct Metadata));
	cpBodySetUserData(body, metadata);
	metadata->type = mt;
	metadata->team = team;
	metadata->control_flags = 0;
	metadata->shoot_timer = 0;
	metadata->scrap_timer = 30000;

	cpShape *shape = cpSpaceAddShape(world_space, cpCircleShapeNew(body, radius, cpvzero));
	cpShapeSetFriction(shape, 0.7);

	cpShapeSetCollisionType(shape, team);
	cpShapeSetFilter(shape, cpShapeFilterNew(CP_NO_GROUP, team, CP_ALL_CATEGORIES));

/*  Faked top down friction from Chipmunk example - does not limit max speed
	if (mt == MT_TANK || mt == MT_SCRAP)
	{
		cpBody *static_body = cpSpaceGetStaticBody(world_space);
		cpConstraint *pivot = cpSpaceAddConstraint(world_space, cpPivotJointNew2(static_body, body, cpvzero, cpvzero));
		cpConstraintSetMaxBias(pivot, 0.0f); // disable joint correction
		cpConstraintSetMaxForce(pivot, 490.0f * mass);

		cpConstraint *gear = cpSpaceAddConstraint(world_space, cpGearJointNew(static_body, body, 0.0f, 1.0f));
		cpConstraintSetMaxBias(gear, 0.0f); // disable joint correction
		cpConstraintSetMaxForce(gear, 16.0f * moment);
	}
*/

	return body;
}

void world_kill_body(cpBody *body)
{
	cbBodyFree(body, NULL);
}

void cbKillBody(cpSpace *space, void *obj, void *data)
{
	world_kill_body((cpBody *)obj);
}

void world_process(int ms)
{
	cpSpaceStep(world_space, ms / 1000.0);
}

void world_draw(cpVect *cc)
{
	cpVect camera_center = *cc;
	camera_center.x -= SCREEN_WIDTH / 2;
	camera_center.y -= SCREEN_HEIGHT / 2;

	SDL_FillRect(screen, NULL,
		SDL_MapRGB(screen->format, 0xcc, 0xcc, 0xcc));
	int yy = 16 - (int)camera_center.y & 0x0f;
	int xx = 16 - (int)camera_center.x & 0x0f;
	for (int y = yy; y < SCREEN_HEIGHT; y += 16)
		for (int x = xx; x < SCREEN_WIDTH; x += 16)
		{
			pixelColor(screen, x, y, 0x777777ff);
		}

	cpSpaceEachBody(world_space, cbBodyDraw, &camera_center);
}

void tank_shoot(cpBody *body)
{
	cpVect pos = cpBodyGetPosition(body);
	cpVect rot = cpvperp(cpBodyGetRotation(body));
	struct Metadata *md = cpBodyGetUserData(body);

	pos.x += 15 * rot.x;
	pos.y += 15 * rot.y;
	cpBody *bullet = world_spawn_body(pos.x, pos.y, MT_BULLET, TEAM_NEUTRAL);
	cpBodySetVelocity(bullet, cpBodyGetVelocity(body));
	cpFloat vbullet = 3000.0f * cpBodyGetMass(body) / cpBodyGetMass(bullet);
	cpFloat vtank = -3000.0f * cpBodyGetMass(bullet) / cpBodyGetMass(body);
	cpBodyApplyImpulseAtLocalPoint(bullet, cpvmult(rot, vbullet), cpvzero);
	cpBodyApplyImpulseAtWorldPoint(body, cpvmult(rot, vtank), cpBodyGetPosition(body));
}

void cbTankShoot(cpSpace *space, void *obj, void *data)
{
	tank_shoot((cpBody *)obj);
}

void cbSumGradients(cpShape *shape, cpVect point, cpFloat distance, cpVect gradient, void *data)
{
	// do not count self
	if (distance < 0.0f)
		return;
	cpVect *vec = data;
	cpBody *boid = cpShapeGetBody(shape);

	// ignore stranded
	struct Metadata *md = cpBodyGetUserData(boid);
	if (!(md->control_flags & CF_FORWARD))
		return;

	// boid separation
	gradient = cpvmult(gradient, 300.0f / (distance * distance));
	*vec = cpvadd(*vec, gradient);

	// boid alignment
	*vec = cpvadd(*vec, cpvperp(cpBodyGetRotation(boid)));
}

void cbBodyDraw(cpBody *body, void *data)
{
	cpBodyEachShape(body, cbShapeDraw, data);

	cpFloat mass = cpBodyGetMass(body);
	cpFloat moment = cpBodyGetMoment(body);
	struct Metadata *md = cpBodyGetUserData(body);

	// apply AI
	if (md && md->type == MT_TANK && md->team == TEAM_ENEMY)
	{
		cpVect target = cpBodyGetPosition(player);
		cpVect pos = cpBodyGetPosition(body);
		cpFloat angle = cpBodyGetAngle(body) + M_PI_2;
		target = cpvsub(target, pos);

		cpVect go_vec = cpvzero;
		cpSpacePointQuery(world_space, pos, 100.0f,
			cpShapeFilterNew(CP_NO_GROUP, TEAM_ENEMY, TEAM_ENEMY),
			cbSumGradients, &go_vec);
		go_vec = cpvadd(cpvmult(target, 25.0f / cpvlengthsq(target)), cpvnormalize(go_vec));
		cpFloat target_angle = atan2(go_vec.y, go_vec.x) - angle;
		target_angle = atan2(sin(target_angle), cos(target_angle));

		md->control_flags = 0;
		if (fabsf(target_angle) < M_PI_2)
			md->control_flags = CF_FORWARD;

		if (target_angle > 0)
			md->control_flags |= CF_TURN_RIGHT;
		else
			md->control_flags |= CF_TURN_LEFT;
	}

	if (md && md->type == MT_TANK && md->team == TEAM_ALLY)
	{
		cpVect pos = cpBodyGetPosition(body);
		cpShape *target_shape = cpSpacePointQueryNearest(world_space, pos, 1000.0f,
			cpShapeFilterNew(CP_NO_GROUP, TEAM_ALLY, TEAM_ENEMY), NULL);

		if (target_shape)
		{
			// get in the way of an enemy
			cpBody *target_body = cpShapeGetBody(target_shape);
			cpVect target = cpBodyGetPosition(target_body);
			cpFloat angle = cpBodyGetAngle(body) + M_PI_2;
			target = cpvsub(target, pos);
			cpFloat target_angle = atan2(target.y, target.x) - angle;
			target_angle = atan2(sin(target_angle), cos(target_angle));

			md->control_flags = CF_FORWARD;
			if (target_angle > 0)
				md->control_flags |= CF_TURN_RIGHT;
			else
				md->control_flags |= CF_TURN_LEFT;
		}
		else
		{
			// follow the player
			cpVect target = cpBodyGetPosition(player);
			cpFloat angle = cpBodyGetAngle(body) + M_PI_2;
			target = cpvsub(target, pos);
			cpFloat target_angle = atan2(target.y, target.x) - angle;
			target_angle = atan2(sin(target_angle), cos(target_angle));

			md->control_flags = 0;
			if (cpvlengthsq(target) > 4900.0f)
				md->control_flags = CF_FORWARD;
			if (target_angle > 0)
				md->control_flags |= CF_TURN_RIGHT;
			else
				md->control_flags |= CF_TURN_LEFT;
		}
	}

	// apply controls
	if (md && md->type == MT_TANK)
	{
		cpFloat speed_unit = 100.0f;
		if (md->team == TEAM_ENEMY || md->team == TEAM_ALLY)
		{
			speed_unit = 150.0f;
		}

		if (md->control_flags & CF_FORWARD)
		{
			cpBodyApplyForceAtLocalPoint(body, cpv(0, 5.0f * speed_unit * mass), cpvzero);
		}
		if (md->control_flags & CF_REVERSE)
		{
			cpBodyApplyForceAtLocalPoint(body, cpv(0, -4.0f * speed_unit * mass), cpvzero);
		}
		if (md->control_flags & CF_TURN_LEFT)
		{
			cpFloat torque = cpBodyGetTorque(body);
			cpBodySetTorque(body, torque - 20.0f * moment);
		}
		if (md->control_flags & CF_TURN_RIGHT)
		{
			cpFloat torque = cpBodyGetTorque(body);
			cpBodySetTorque(body, torque + 20.0f * moment);
		}
		if (md->control_flags & CF_SHOOT)
		{
			md->shoot_timer -= TIME_STEP;
			if (md->shoot_timer < 0)
			{
				md->shoot_timer = SHOOT_RELOAD;
				cpSpaceAddPostStepCallback(world_space, cbTankShoot, body, NULL);
			}
		}
	}

	// apply physics
	if (md && (md->type == MT_TANK || md->type == MT_SCRAP))
	{
		cpVect kinetic = cpBodyGetVelocity(body);
		kinetic = cpvmult(kinetic, -5.0f * mass);
		cpBodyApplyForceAtWorldPoint(body, kinetic, cpBodyGetPosition(body));

		cpFloat kinetic_angular = cpBodyGetAngularVelocity(body);
		kinetic_angular = -5.0f * kinetic_angular * moment + cpBodyGetTorque(body);
		cpBodySetTorque(body, kinetic_angular);
	}

	// destroy idle bullets
	if (md && md->type == MT_BULLET)
	{
		cpFloat kenergy = cpBodyKineticEnergy(body);
		if (kenergy < 600000.0f)
			cpSpaceAddPostStepCallback(world_space, cbKillBody, body, NULL);
	}

	// scrap enemies
	if (md && md->type == MT_TANK && md->team == TEAM_ENEMY)
	{
		md->scrap_timer -= TIME_STEP;
		if (md->scrap_timer < 0)
		{
			md->type = MT_SCRAP;
			cpBodyEachShape(body, cbScrap, NULL);
		}
	}
}

void cbShapeDraw(cpBody *body, cpShape *shape, void *data)
{
	cpVect camera_center = *(cpVect *)data;

	cpVect pos = cpvsub(cpBodyGetPosition(body), camera_center);
	struct Metadata *md = cpBodyGetUserData(body);

	Uint32 color = 0x555555ff;
	if (md->type == MT_TANK)
	{
		if (md->team == TEAM_PLAYER)
			color = 0x5555ccff;
		else if (md->team == TEAM_ENEMY)
			color = 0xcc5555ff;
		else if (md->team == TEAM_ALLY)
			color = 0x55cc55ff;
	}
	Sint16 r = cpCircleShapeGetRadius(shape);
	int x = roundf(pos.x);
	int y = roundf(pos.y);
	filledCircleColor(screen, x, y, r, color);
	circleColor(screen, x, y, r, 0x000000ff);

	if (md->type == MT_TANK)
	{
		cpVect lend = cpvadd(cpvmult(cpvperp(cpBodyGetRotation(body)), 1.5f * r), pos);
		lineColor(screen, x, y, lend.x, lend.y, 0x000000ff);
	}
}

void cbBodyFree(cpBody *body, void *data)
{
	struct Metadata *md = cpBodyGetUserData(body);
	if (md)
		free(md);

	cpBodyEachShape(body, cbShapeFree, data);

	if (!cpSpaceIsLocked(world_space))
		cpSpaceRemoveBody(world_space, body);

	cpBodyFree(body);
}

void cbShapeFree(cpBody *body, cpShape *shape, void *data)
{
	if (!cpSpaceIsLocked(world_space))
		cpSpaceRemoveShape(world_space, shape);
	cpShapeFree(shape);
}

void cbScrap(cpBody *body, cpShape *shape, void *data)
{
	cpShapeSetCollisionType(shape, TEAM_SCRAP);
	cpShapeSetFilter(shape, cpShapeFilterNew(CP_NO_GROUP, TEAM_SCRAP, CP_ALL_CATEGORIES));
}

void cbOnPlayerEnemyTouch(cpArbiter *arb, cpSpace *space, cpDataPointer data)
{
	if (0 == gameover_time)
	{
		gameover_time = game_timer;
		struct Metadata *md = cpBodyGetUserData(player);
		md->control_flags = 0;
	}
}
