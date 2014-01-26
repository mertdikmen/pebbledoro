#include <pebble.h>

#define ANIMATED true
#define NOT_ANIMATED false

//#define _DEBUG

#ifdef _DEBUG
#define ATOMIC_POM_DURATION 500 //milliseconds
#define ATOMIC_BREAK_DURATION 100
#else
#define ATOMIC_POM_DURATION 60000
#define ATOMIC_BREAK_DURATION 12000
#endif

#define POMODORO_COUNT_PKEY 0

static const size_t rows = 5;
static const size_t cols = 5;

static Window *main_window;
static Window *menu_window;
static AppTimer* app_timer;
static TextLayer* text_layer;
static ActionBarLayer* actionbar_layer;

static SimpleMenuLayer *simple_menu_layer;
#define NUM_MENU_SECTIONS 1
static SimpleMenuSection menu_sections[NUM_MENU_SECTIONS];
#define NUM_FIRST_SECTION_ITEMS 1
static SimpleMenuItem first_section_items[NUM_FIRST_SECTION_ITEMS];

static BitmapLayer** minute_layers;
static GColor minute_color;

static GBitmap* action_icon_play;
static GBitmap* action_icon_stop;
static GBitmap* action_icon_stat;

static GRect bounds; 

static const GPathInfo SQUARE_POINTS = {
    4,
    (GPoint []) {
        { 0, 0},
        { 0, 19},
        { 19, 19},
        { 19, 0}
    }
};

static const GRect SQUARE_RECT = {
    .origin = { 0, 0 }, .size = { 19, 19 }
};

static GPath* square_path;

static size_t atomic_pom_count;

static char pom_count_buf[100];

static enum POM_STATE {
    IN_PROGRESS,
    DONE,
    BREAK_IN_PROGRESS,
    BREAK_DONE,
} pomodoro_state;

static void action_bar_click_config_provider(void *context);

static void menu_select_callback(int index, void *ctx) {
    //window_stack_pop( true );
    static const uint32_t const segments[] = { 100, 100, 100 };
    VibePattern pat = {
        .durations = segments,
        .num_segments = ARRAY_LENGTH(segments),
    };
    vibes_enqueue_custom_pattern(pat);
}

//adds or removes a tick depending on pomodoro state
static void timer_callback( void* data )
{
    if ( pomodoro_state == IN_PROGRESS )
    {
        size_t i = atomic_pom_count / cols;
        size_t j = atomic_pom_count % cols;

        BitmapLayer* cur_square = minute_layers[ i * 5 + j ];
        //bitmap_layer_set_background_color( cur_square, GColorBlack );
        //layer_mark_dirty( (Layer*) cur_square ); //bitmap_layer_get_layer( cur_square ) );
        layer_set_hidden( bitmap_layer_get_layer( cur_square ), false );
        atomic_pom_count++;
        if ( atomic_pom_count == 25 )
        {
            static const uint32_t const segments[] = { 100, 100, 100 };
            VibePattern pat = {
                .durations = segments,
                .num_segments = ARRAY_LENGTH(segments),
            };
            vibes_enqueue_custom_pattern(pat);
            text_layer_set_text( text_layer, "Pomodoro finished!" );
            pomodoro_state = DONE;

            int32_t pom_count = persist_read_int( POMODORO_COUNT_PKEY );
            persist_write_int( POMODORO_COUNT_PKEY, pom_count + 1 );

            return;
        }
        app_timer = app_timer_register(ATOMIC_POM_DURATION, timer_callback, &atomic_pom_count);
    }
    else if ( pomodoro_state == BREAK_IN_PROGRESS ) //break
    {
        size_t i = ( atomic_pom_count - 25 ) / cols;
        size_t j = atomic_pom_count % cols;

        BitmapLayer* cur_square = minute_layers[ i * 5 + j ];
        layer_set_hidden( bitmap_layer_get_layer( cur_square ), true );
        atomic_pom_count++;

        if ( atomic_pom_count == 50 )
        {
            static const uint32_t const segments[] = { 100, 50, 200 };
            VibePattern pat = {
                .durations = segments,
                .num_segments = ARRAY_LENGTH(segments),
            };
            vibes_enqueue_custom_pattern(pat);
            text_layer_set_text( text_layer, "Break finished!" );
            pomodoro_state = BREAK_DONE;
            return;
        }
        app_timer = app_timer_register(ATOMIC_BREAK_DURATION, timer_callback, &atomic_pom_count);
    }
}

//middle button handler.  opens the stats menu
static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
    window_stack_push( menu_window, true );
}

//helper function to clear pomodoro ticks
static void hide_all_pomodoro_ticks()
{
    for ( size_t i = 0; i < rows * cols; i++ )
        layer_set_hidden( bitmap_layer_get_layer( minute_layers[i] ), true );
}

// start button handler
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
    if ( pomodoro_state == DONE ) //start break
    {
        app_timer = app_timer_register(ATOMIC_BREAK_DURATION, timer_callback, &atomic_pom_count);
        text_layer_set_text(text_layer, "Enjoy your break." );
        pomodoro_state = BREAK_IN_PROGRESS;
    }
    if ( pomodoro_state == BREAK_DONE ) //start pomodoro
    {
        hide_all_pomodoro_ticks();
        atomic_pom_count = 0;
        app_timer = app_timer_register(ATOMIC_POM_DURATION, timer_callback, &atomic_pom_count);
        text_layer_set_text(text_layer, "Stay focused." );
        pomodoro_state = IN_PROGRESS;
    }
}

//cancels the pomodoro/break
static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
    if ( pomodoro_state == BREAK_IN_PROGRESS || pomodoro_state == DONE )
    {
        hide_all_pomodoro_ticks();
        text_layer_set_text(text_layer, "Break cancelled." );
        pomodoro_state = BREAK_DONE;
    }
    else if ( pomodoro_state == IN_PROGRESS )
    {
        hide_all_pomodoro_ticks();
        text_layer_set_text(text_layer, "Cancelled." );
        pomodoro_state = BREAK_DONE;
    }
}

static void action_bar_click_config_provider(void *context) {
    window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
    window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
    window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

static void update_square_layer(Layer *layer, GContext* ctx) {
  //static unsigned int angle = 0;
  //gpath_rotate_to(square_path, (TRIG_MAX_ANGLE / 360) * angle);
  //angle = (angle + 5) % 360;

  //graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_fill_color( ctx, minute_color );
  graphics_draw_round_rect( ctx, SQUARE_RECT, 5 );
  graphics_fill_rect( ctx, SQUARE_RECT, 5, GCornersAll );
  //gpath_draw_outline(ctx, square_path);
}

static void window_load( Window* window )
{
    atomic_pom_count = 50;
    pomodoro_state = BREAK_DONE;
    Layer* window_layer = window_get_root_layer( window );
    GRect window_bounds = layer_get_bounds( window_layer );
    bounds = layer_get_bounds( window_layer );

    actionbar_layer = action_bar_layer_create();

    action_bar_layer_set_click_config_provider( actionbar_layer, action_bar_click_config_provider );
    action_bar_layer_set_icon( actionbar_layer, BUTTON_ID_UP, action_icon_play );
    action_bar_layer_set_icon( actionbar_layer, BUTTON_ID_DOWN, action_icon_stop );
    action_bar_layer_set_icon( actionbar_layer, BUTTON_ID_SELECT, action_icon_stat );

    //Message Box
    text_layer = text_layer_create((GRect) { .origin = { 0, 126 }, .size = { window_bounds.size.w - 22, 25 } });
    text_layer_set_text(text_layer, "Start a pomodoro!" );
    text_layer_set_text_alignment(text_layer, GTextAlignmentCenter);
    text_layer_set_background_color( text_layer, GColorWhite );

    //Counting Squares
    minute_layers = malloc( rows * cols * sizeof( BitmapLayer* ) );
    minute_color = GColorBlack;

    for ( size_t i = 0; i < rows; i++ )
    {
        for ( size_t j = 0; j < cols; j++ )
        {
            BitmapLayer* square_layer = bitmap_layer_create( (GRect) { 
                    .origin = { 3 + 25 * j, 3 + 25 * i },
                    .size = { 20, 20 } 
                    });

            minute_layers[ i * cols + j ] = square_layer;
            //bitmap_layer_set_background_color( square_layer, GColorBlack );

            layer_set_update_proc( bitmap_layer_get_layer( square_layer ), update_square_layer );
            layer_add_child( window_layer, bitmap_layer_get_layer( square_layer ) );
        }
    }

    layer_add_child( window_layer, text_layer_get_layer(text_layer));

    hide_all_pomodoro_ticks();

    action_bar_layer_add_to_window( actionbar_layer, window );
}

static void window_unload( Window* window )
{
    for( size_t i = 0; i < rows * cols; i++ )
        bitmap_layer_destroy( minute_layers[i] );

    free( minute_layers );
}

static void menu_window_unload( Window* window ) {

}

static void menu_window_appear( Window* window ) {
    Layer* window_layer = window_get_root_layer( window );

    int32_t pom_ct = persist_read_int( POMODORO_COUNT_PKEY );

    snprintf( pom_count_buf, 100, "%d", (int) pom_ct );

    //The simple menu layer
    first_section_items[ 0 ] = (SimpleMenuItem){
        .title = pom_count_buf,
        .subtitle = "Pomodoros completed",
        .callback = menu_select_callback,
    };

    menu_sections[ 0 ] = (SimpleMenuSection){
        .num_items = NUM_FIRST_SECTION_ITEMS,
        .items = first_section_items,
    };

    //layer_set_hidden( pomodoro_tick_layer, true );
    //Initialize the simple menu layer
    simple_menu_layer = simple_menu_layer_create( bounds, window, menu_sections, NUM_MENU_SECTIONS, NULL );
    // Add it to the window for display
    layer_add_child( window_layer, simple_menu_layer_get_layer( simple_menu_layer ) );
}

static void menu_window_load( Window* window ) {

}

static void menu_window_disappear( Window* window ) {
    layer_remove_from_parent( simple_menu_layer_get_layer( simple_menu_layer ) );
    simple_menu_layer_destroy( simple_menu_layer );
}

static void init( void )
{
    if (!persist_exists( POMODORO_COUNT_PKEY ) ) persist_write_int( POMODORO_COUNT_PKEY, 0 );

    action_icon_play = gbitmap_create_with_resource( RESOURCE_ID_ICON_PLAY );
    action_icon_stop = gbitmap_create_with_resource( RESOURCE_ID_ICON_STOP );
    action_icon_stat = gbitmap_create_with_resource( RESOURCE_ID_ICON_STAT );
    square_path = gpath_create( &SQUARE_POINTS );
    main_window = window_create();
    window_set_window_handlers( main_window, (WindowHandlers) {
            .load = window_load,
            .unload = window_unload, } );

    menu_window = window_create();

    window_set_window_handlers( menu_window, (WindowHandlers) {
            .load = menu_window_load,
            .unload = menu_window_unload,
            .appear = menu_window_appear,
            .disappear = menu_window_disappear } );

    window_stack_push( main_window, ANIMATED );
}

static void deinit( void )
{
    window_destroy( menu_window );
    window_destroy( main_window );
    gpath_destroy( square_path );
}

int main( void ){
    init();
    app_event_loop();
    deinit();
}
