#include <SDL3/SDL_scancode.h>
#include <SDL3/SDL_stdinc.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define SDL_MAIN_USE_CALLBACKS

#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>

#define GRAVITY 1000
#define NUMBER_OF_BALLS 50000
#define SUB_STEPS 8
#define BLACK_HOLE 5000
#define WINDOW_X 1920
#define WINDOW_Y 800
#define PARTICLE_SIZE 2
#define CELL_SIZE 4

typedef struct {
    double x;
    double y;
} Vector;

typedef struct {
    double radius;
    double x;
    double y;
} Circle;

typedef struct {
    Circle circle;
    Vector previous_position;
    Vector current_position;
    Vector acceleration;
    double size;
    unsigned int id;
    int red;
    int green;
    int blue;
} Particle;

typedef struct ParticleLinkedList {
    Particle *particle;
    struct ParticleLinkedList *next;
} ParticleLinkedList;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;

    // Physics objects
    Particle *particle[NUMBER_OF_BALLS];
    int number_of_balls;
    Circle *permiter;
    Uint64 previous_time;

    // Cool as fuck grid
    ParticleLinkedList *grid[NUMBER_OF_BALLS * 2];

    // Input
    bool space_held;
    bool create_ball;
    bool collisions_enabled;
    bool can_toggle_collisions;
    bool can_reset;
    bool black_hole;
    bool destroy_ball;
    bool can_apply_drag;
    double drag;
    bool can_change_amount;
    int amount;
} Appstate;

static inline Vector Add_Vector(Vector vec1, Vector vec2){
    Vector vec0;

    vec0.x = vec1.x + vec2.x;
    vec0.y = vec1.y + vec2.y;

    return  vec0;
}

static inline Vector Sub_Vector(Vector vec1, Vector vec2){
    Vector vec0;

    vec0.x = vec1.x - vec2.x;
    vec0.y = vec1.y - vec2.y;

    return  vec0;
}

static inline Vector Mul_Vector(Vector vec1, double multiplier){
    Vector vec0;
    
    vec0.x = vec1.x * multiplier;
    vec0.y = vec1.y * multiplier;

    return vec0;
}

void Clear_Grid(Appstate *state){
    memset(state->grid, 0, sizeof(state->grid));
}

ParticleLinkedList* Create_Particle_Linked_List_Node(Particle *particle){
    ParticleLinkedList *particle_linked_list = (ParticleLinkedList*)malloc(sizeof(ParticleLinkedList));
    particle_linked_list->particle = particle;
    particle_linked_list->next = NULL;

    return particle_linked_list;
}

void Add_To_Particle_Linked_List(Particle *particle, ParticleLinkedList **head){
    ParticleLinkedList *new_node = Create_Particle_Linked_List_Node(particle);

    if (*head == NULL){
        *head = new_node;
    } else {
        ParticleLinkedList *temp = *head;
        while (temp->next != NULL){
            temp = temp->next;
        }
        temp->next = new_node;
    }
}

void Destroy_Particle_Linked_List(ParticleLinkedList *head){
    ParticleLinkedList *temp;

    while (head != NULL) {
        temp = head;
        head = head->next;
        free(temp);
    }
}

static inline unsigned int Hash(Vector *position){
    unsigned int xi = (unsigned int)SDL_floor(position->x / CELL_SIZE);
    unsigned int yi = (unsigned int)SDL_floor(position->y / CELL_SIZE);

    unsigned int h = (xi * 92837111) ^ (yi * 689287499);

    return h % (2 * NUMBER_OF_BALLS);
}

void Add_Particles_To_Grid(Appstate *state){
    for (int i = 0; i < state->number_of_balls; ++i){
        Vector particle_position = state->particle[i]->current_position;
        unsigned int h = Hash(&particle_position);
        Add_To_Particle_Linked_List(state->particle[i], &state->grid[h]);
    }
}

void Destroy_Particles_From_Grid(Appstate *state){
    for (int i = 0; i < NUMBER_OF_BALLS; ++i){
        if (state->grid[i] == NULL) continue;

        Destroy_Particle_Linked_List(state->grid[i]);
    }

    Clear_Grid(state);
}

double Mag_Vector(Vector vec0) {
    return SDL_sqrt(vec0.x * vec0.x + vec0.y * vec0.y);
}

double Square_Mag_Vector(Vector vec0){
    return vec0.x * vec0.x + vec0.y * vec0.y;
}

void Draw_Circle(SDL_Renderer *renderer, Circle *circle) {
    double x = circle->radius;
    double y = 0;
    double err = 0;

    while (x >= y) {
        SDL_RenderPoint(renderer, circle->x + x, circle->y + y);
        SDL_RenderPoint(renderer, circle->x + y, circle->y + x);
        SDL_RenderPoint(renderer, circle->x - y, circle->y + x);
        SDL_RenderPoint(renderer, circle->x - x, circle->y + y);
        SDL_RenderPoint(renderer, circle->x - x, circle->y - y);
        SDL_RenderPoint(renderer, circle->x - y, circle->y - x);
        SDL_RenderPoint(renderer, circle->x + y, circle->y - x);
        SDL_RenderPoint(renderer, circle->x + x, circle->y - y);

        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void draw_filled_circle(SDL_Renderer *renderer, Circle *circle) {
    for (int y = -circle->radius; y <= circle->radius; y++) {
        for (int x = -circle->radius; x <= circle->radius; x++) {
            if (x*x + y*y <= circle->radius*circle->radius) { // Inside the circle
                SDL_RenderPoint(renderer, circle->x + x, circle->y + y);
            }
        }
    }
}


void DestroyCircle(Circle *circle){
    free(circle);
}

void DestroyParticle(Particle *particle){
    free(particle);
}

void Accelerate(Particle *particle, Vector acceleration) {
    particle->acceleration = Add_Vector(particle->acceleration, acceleration);
}

void ApplyGravity(Particle **particle, Appstate *state){
    for (int i = 0; i < state->number_of_balls; ++i){
        Vector gravity = {0, GRAVITY};
        Accelerate(particle[i], gravity);
    }
}

void ApplyConstraint(Particle **particle, Appstate *state){
    for (int i = 0; i < state->number_of_balls; ++i){
        Vector permiter_centre = {(double) WINDOW_X / 2,(double) WINDOW_Y / 2};
        Vector distance = Sub_Vector(particle[i]->current_position, permiter_centre);

        if (Mag_Vector(distance) > (double) WINDOW_Y / 2 - particle[i]->size) {
            Vector direction = Mul_Vector(distance, 1/(Mag_Vector(distance)));
            particle[i]->current_position = Add_Vector(permiter_centre, Mul_Vector(direction, (double) WINDOW_Y / 2 - particle[i]->size));
        }
    }
}

void UpdatePosition(Particle **particle, double delta_time, Appstate *state) {

    for (int i = 0; i < state->number_of_balls; ++i){

        Vector velocity = Sub_Vector(particle[i]->current_position, particle[i]->previous_position);

        velocity = Mul_Vector(velocity, 1 - state->drag * Mag_Vector(velocity));

        particle[i]->previous_position = particle[i]->current_position;

        particle[i]->current_position = Add_Vector(particle[i]->current_position, Add_Vector(velocity, Mul_Vector(particle[i]->acceleration, delta_time * delta_time)));

        particle[i]->acceleration.x = 0;
        particle[i]->acceleration.y = 0;
    }
}

void ApplyThrust(Particle **particle, Appstate *state) {
    for (int i = 0; i < state->number_of_balls; ++i){
        Vector thrust = {0, -2000};
        Accelerate(particle[i], thrust);
    }
}

void ApplyBlackHole(Particle **particle, Appstate *state){
    Vector centre = {(double) WINDOW_X / 2, (double) WINDOW_Y / 2};
    for (int i = 0; i < state->number_of_balls; ++i){
        Vector displacement = Sub_Vector(centre, particle[i]->current_position);
        Vector direction = Mul_Vector(displacement, 1/Mag_Vector(displacement));

        Vector acceleration = Mul_Vector(direction, BLACK_HOLE);

        Accelerate(particle[i], acceleration);
    }
}

void SolveCollisions(Particle **particle, Appstate *state) {
    for (int i = 0; i < state->number_of_balls; ++i){
        ParticleLinkedList *nodes[9];

        int xp = particle[i]->current_position.x;
        int yp = particle[i]->current_position.y;

        int count = 0;
        for (int x = -1; x <= 1; ++x){
            for (int y = -1; y <= 1; ++y){
                int xg = (SDL_floor(((double) xp + (CELL_SIZE * x)) / CELL_SIZE) * CELL_SIZE) + (int)(CELL_SIZE / 2);
                int yg = (SDL_floor(((double) yp + (CELL_SIZE * y)) / CELL_SIZE) * CELL_SIZE) + (int) (CELL_SIZE / 2);

                Vector grid_position = {xg, yg};
                
                nodes[count] = state->grid[Hash(&grid_position)];
                count += 1;
            }
        }

        for (int j = 0; j < 9; ++j){
            if (nodes[j] == NULL){
                continue;
            }

            ParticleLinkedList *node = nodes[j];

            while (node != NULL){
                if (particle[i]->id == node->particle->id){
                    node = node->next;
                } else {
                    Vector distance = Sub_Vector(particle[i]->current_position,node->particle->current_position);
                    double distance_magnitude = Mag_Vector(distance);
                    if (distance_magnitude < particle[i]->size + node->particle->size && distance_magnitude != 0) {
                        Vector direction = Mul_Vector(distance, 1/distance_magnitude);
                        double delta = particle[i]->size + node->particle->size - distance_magnitude;
                        particle[i]->current_position = Add_Vector(particle[i]->current_position, Mul_Vector(direction, 0.5 * delta));
                        node->particle->current_position = Sub_Vector(node->particle->current_position, Mul_Vector(direction, 0.5 * delta));
                    }

                    node = node->next;
                }
            }
        }
    }
}

void PhysicsUpdate(Particle *particle[NUMBER_OF_BALLS], Appstate *state){

    if ((double)(SDL_GetPerformanceCounter() - state->previous_time) / SDL_GetPerformanceFrequency() < 0.016){
        return;
    }

    state->previous_time = SDL_GetPerformanceCounter();


    for (int i = 0; i < state->amount; ++i){

        if (state->create_ball && state->number_of_balls < NUMBER_OF_BALLS){
            Particle *particle = (Particle*) malloc(sizeof(Particle));

            Vector initialPosition = {660,400};
            Vector initialAcceleraton = {0,0};

            double random_size = PARTICLE_SIZE;

            particle->circle.radius = random_size;
            particle->circle.x = initialPosition.x;
            particle->circle.y = initialPosition.y;
        
            particle->previous_position = initialPosition;
            particle->current_position = initialPosition;
        
            particle->acceleration = initialAcceleraton;

            particle->size = random_size;

            particle->id = state->number_of_balls;

            particle->red = SDL_rand(256);
            particle->green = SDL_rand(256);
            particle->blue = SDL_rand(256);

            state->particle[state->number_of_balls] = particle;
            state->number_of_balls += 1;
        }

        if (state->number_of_balls == NUMBER_OF_BALLS){
            break;
        }
    }

    if (state->destroy_ball && state->number_of_balls != 0){
        DestroyParticle(state->particle[state->number_of_balls - 1]);
        state->number_of_balls -= 1;
    }

    Add_Particles_To_Grid(state);

    for (int i = 0; i < SUB_STEPS; ++i){



        if (!state->black_hole){
            ApplyGravity(particle, state);
        }

        if (state->space_held)
            ApplyThrust(particle, state);

        if (state->black_hole)
            ApplyBlackHole(particle, state);

        ApplyConstraint(particle, state);

        if (state->collisions_enabled){
            SolveCollisions(particle, state);
        }
        UpdatePosition(particle, 0.016 / SUB_STEPS, state);

    }
    Destroy_Particles_From_Grid(state);

}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    Appstate *state = (Appstate*)appstate;
    SDL_DestroyWindow(state->window);
    SDL_DestroyRenderer(state->renderer);
    for (int i = 0; i < state->number_of_balls; ++i){
        DestroyParticle(state->particle[i]);
        state->particle[i] = NULL;
    }
    DestroyCircle(state->permiter);
    state->permiter = NULL;
    free(appstate);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event){
    Appstate *state = (Appstate*)appstate;

    if (event->type == SDL_EVENT_KEY_DOWN){
        if (event->key.scancode == SDL_SCANCODE_SPACE){
            state->space_held = true;
        }

        if (event->key.scancode == SDL_SCANCODE_W){
            state->create_ball = true;
        }

        if (event->key.scancode == SDL_SCANCODE_S){
            state->destroy_ball = true;
        }

        if (event->key.scancode == SDL_SCANCODE_H && state->can_toggle_collisions) {
            state->collisions_enabled = !state->collisions_enabled;
            state->can_toggle_collisions = false;
        }

        if (event->key.scancode == SDL_SCANCODE_R && state->can_reset){
            state->can_reset = false;

            for (int i = 0; i < state->number_of_balls; ++i){
                DestroyParticle(state->particle[i]);
            }

            state->number_of_balls = 0;
        }

        if (event->key.scancode == SDL_SCANCODE_G) {
            state->black_hole = true;
        }

        if (event->key.scancode == SDL_SCANCODE_O && state->can_apply_drag) {
            state->can_apply_drag = false;
            state->drag += 0.0001;

            printf("Current Drage: %f\n", state->drag);
        }

        if (event->key.scancode == SDL_SCANCODE_L && state->can_apply_drag){
            state->can_apply_drag = false;
            if (state->drag > 0) state->drag -= 0.0001;

            printf("Current Drage: %f\n", state->drag);
        }
        
        if (event->key.scancode == SDL_SCANCODE_I && state->can_change_amount) {
            state->can_change_amount = false;
            state->amount += 1;

            printf("Current Amount = %d\n", state->amount);
        }

        if (event->key.scancode == SDL_SCANCODE_K && state->can_change_amount){
            state->can_change_amount = false;

            if (state->amount > 1) state->amount -= 1;

            printf("Current Amount = %d\n", state->amount);
        }
    }

    if (event->type == SDL_EVENT_KEY_UP){
        if (event->key.scancode == SDL_SCANCODE_SPACE){
            state->space_held = false;
        }

        if (event->key.scancode == SDL_SCANCODE_P) {
            printf("Number Of Balls: %d\n", state->number_of_balls);
        }

        if (event->key.scancode == SDL_SCANCODE_W){
            state->create_ball = false;
        }

        if (event->key.scancode == SDL_SCANCODE_H) {
            state->can_toggle_collisions = true;
        }

        if (event->key.scancode == SDL_SCANCODE_R) {
            state->can_reset = true;
        }

        if (event->key.scancode == SDL_SCANCODE_G) {
            state->black_hole = false;
        }

        if (event->key.scancode == SDL_SCANCODE_S) {
            state->destroy_ball = false;
        }

        if (event->key.scancode == SDL_SCANCODE_O) {
            state->can_apply_drag = true;
        }

        if (event->key.scancode == SDL_SCANCODE_L){
            state->can_apply_drag = true;
        }

        if (event->key.scancode == SDL_SCANCODE_I){
            state->can_change_amount = true;
        }

        if (event->key.scancode == SDL_SCANCODE_K){
            state->can_change_amount = true;
        }

    }

    if (event->type == SDL_EVENT_QUIT){
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {

    Appstate *state = (Appstate*) appstate;

    PhysicsUpdate(state->particle, appstate);

    SDL_SetRenderDrawColor(state->renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);

    SDL_RenderClear(state->renderer);

    if (state->collisions_enabled){

        for (int i = 0; i < state->number_of_balls; ++i){

            state->particle[i]->circle.x = state->particle[i]->current_position.x;
            state->particle[i]->circle.y = state->particle[i]->current_position.y;

            SDL_SetRenderDrawColor(state->renderer, state->particle[i]->red, state->particle[i]->green, state->particle[i]->blue, SDL_ALPHA_OPAQUE);

            draw_filled_circle(state->renderer, &state->particle[i]->circle);
        }
    } else {
        for (int i = 0; i < state->number_of_balls; ++i){

            state->particle[i]->circle.x = state->particle[i]->current_position.x;
            state->particle[i]->circle.y = state->particle[i]->current_position.y;

            SDL_SetRenderDrawColor(state->renderer, state->particle[i]->red, state->particle[i]->green, state->particle[i]->blue, SDL_ALPHA_OPAQUE);

            Draw_Circle(state->renderer, &state->particle[i]->circle);
        }
    }
    SDL_SetRenderDrawColor(state->renderer, 255, 0, 0, SDL_ALPHA_OPAQUE);

    Draw_Circle(state->renderer, state->permiter);

    SDL_RenderPresent(state->renderer);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    *appstate = malloc(sizeof(Appstate));
    Appstate *state = (Appstate*)*appstate;

    if (!SDL_Init(SDL_INIT_VIDEO)){
        SDL_Log("Failed to initialise SDL: %s", SDL_GetError());
    }

    state->window = SDL_CreateWindow("Ball simulation", WINDOW_X, WINDOW_Y, SDL_WINDOW_RESIZABLE);
    if (!state->window) {
        SDL_Log("Failed to create window: %s", SDL_GetError());
    }

    state->renderer = SDL_CreateRenderer(state->window, NULL);
    if (!state->renderer){
        SDL_Log("Failed to create renderer: %s", SDL_GetError());
    }

    Circle *permiter = (Circle*) malloc(sizeof(Circle));

    permiter->radius = (double) WINDOW_Y / 2;
    permiter->x = (double) WINDOW_X / 2;
    permiter->y = (double) WINDOW_Y / 2;

    state->permiter = permiter;

    state->previous_time = SDL_GetPerformanceCounter();
    
    state->space_held = false;

    state->number_of_balls = 0;

    state->create_ball = false;

    state->collisions_enabled = true;

    state->can_toggle_collisions = true;

    state->can_reset = true;

    state->black_hole = false;

    state->destroy_ball = false;

    state->drag = 0;

    state->can_apply_drag = true;

    state->amount = 1;

    state->can_change_amount = true;

    Clear_Grid(state);

    return SDL_APP_CONTINUE;
}