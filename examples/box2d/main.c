#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>

#include "box2d/box2d.h"
#include "box2d/math_functions.h"

#define WINDOW_WIDTH  960
#define WINDOW_HEIGHT 640

static b2WorldId g_worldId = { 0 };
static int g_currentScene = 1;
static bool g_paused = false;

static float g_cameraX = 0.0f;
static float g_cameraY = 8.0f;
static float g_zoom = 24.0f; // pixels per meter

static bool g_isDragging = false;
static b2BodyId g_dragBody = { 0 };

static b2BodyId g_carChassis = { 0 };

static int g_bodyCount = 0;
static int g_jointCount = 0;
static float g_fps = 0.0f;

// Coordinate transformation: Box2D world -> Screen pixels
static POINT WorldToScreen(b2Vec2 p)
{
    POINT pt;
    float cx = WINDOW_WIDTH * 0.5f;
    float cy = WINDOW_HEIGHT * 0.7f;
    pt.x = (LONG)(cx + (p.x - g_cameraX) * g_zoom);
    pt.y = (LONG)(cy - (p.y - g_cameraY) * g_zoom);
    return pt;
}

// Screen pixels -> Box2D world
static b2Vec2 ScreenToWorld(int sx, int sy)
{
    float cx = WINDOW_WIDTH * 0.5f;
    float cy = WINDOW_HEIGHT * 0.7f;
    b2Vec2 p;
    p.x = g_cameraX + ((float)sx - cx) / g_zoom;
    p.y = g_cameraY + (cy - (float)sy) / g_zoom;
    return p;
}

static COLORREF HexToRGB(b2HexColor hex)
{
    uint8_t r = (hex >> 16) & 0xFF;
    uint8_t g = (hex >> 8) & 0xFF;
    uint8_t b = hex & 0xFF;
    return RGB(r, g, b);
}

// Debug Draw Callbacks
static void MyDrawSolidPolygon(b2WorldTransform transform, const b2Vec2* vertices, int vertexCount, float radius, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    POINT pts[32];
    if (vertexCount > 32) vertexCount = 32;
    for (int i = 0; i < vertexCount; ++i)
    {
        b2Vec2 wp = b2TransformPoint(transform, vertices[i]);
        pts[i] = WorldToScreen(wp);
    }
    COLORREF c = HexToRGB(color);
    HBRUSH hBrush = CreateSolidBrush(c);
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(min(GetRValue(c) + 40, 255), min(GetGValue(c) + 40, 255), min(GetBValue(c) + 40, 255)));
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    Polygon(hdc, pts, vertexCount);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

static void MyDrawPolygon(b2WorldTransform transform, const b2Vec2* vertices, int vertexCount, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    POINT pts[32];
    if (vertexCount > 32) vertexCount = 32;
    for (int i = 0; i < vertexCount; ++i)
    {
        b2Vec2 wp = b2TransformPoint(transform, vertices[i]);
        pts[i] = WorldToScreen(wp);
    }
    HPEN hPen = CreatePen(PS_SOLID, 1, HexToRGB(color));
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Polygon(hdc, pts, vertexCount);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(hPen);
}

static void MyDrawSolidCircle(b2WorldTransform transform, b2Vec2 center, float radius, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    b2Vec2 wp = b2TransformPoint(transform, center);
    POINT p = WorldToScreen(wp);
    int r = (int)(radius * g_zoom);
    if (r < 1) r = 1;

    COLORREF c = HexToRGB(color);
    HBRUSH hBrush = CreateSolidBrush(c);
    HPEN hPen = CreatePen(PS_SOLID, 1, RGB(min(GetRValue(c) + 40, 255), min(GetGValue(c) + 40, 255), min(GetBValue(c) + 40, 255)));
    HGDIOBJ oldBrush = SelectObject(hdc, hBrush);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    Ellipse(hdc, p.x - r, p.y - r, p.x + r, p.y + r);

    // Rotation indicator line
    b2Vec2 spoke = b2TransformPoint(transform, (b2Vec2){ center.x + radius, center.y });
    POINT sp = WorldToScreen(spoke);
    MoveToEx(hdc, p.x, p.y, NULL);
    LineTo(hdc, sp.x, sp.y);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hBrush);
    DeleteObject(hPen);
}

static void MyDrawCircle(b2Pos center, float radius, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    POINT p = WorldToScreen(center);
    int r = (int)(radius * g_zoom);
    if (r < 1) r = 1;

    HPEN hPen = CreatePen(PS_SOLID, 1, HexToRGB(color));
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, p.x - r, p.y - r, p.x + r, p.y + r);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBrush);
    DeleteObject(hPen);
}

static void MyDrawSolidCapsule(b2Pos p1, b2Pos p2, float radius, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    POINT pt1 = WorldToScreen(p1);
    POINT pt2 = WorldToScreen(p2);
    int penWidth = (int)(radius * 2.0f * g_zoom);
    if (penWidth < 1) penWidth = 1;

    HPEN hPen = CreatePen(PS_SOLID | PS_ENDCAP_ROUND, penWidth, HexToRGB(color));
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    MoveToEx(hdc, pt1.x, pt1.y, NULL);
    LineTo(hdc, pt2.x, pt2.y);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);
}

static void MyDrawLine(b2Pos p1, b2Pos p2, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    POINT pt1 = WorldToScreen(p1);
    POINT pt2 = WorldToScreen(p2);
    HPEN hPen = CreatePen(PS_SOLID, 2, HexToRGB(color));
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    MoveToEx(hdc, pt1.x, pt1.y, NULL);
    LineTo(hdc, pt2.x, pt2.y);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);
}

static void MyDrawTransform(b2WorldTransform transform, void* context)
{
    HDC hdc = (HDC)context;
    b2Pos p = transform.p;
    b2Vec2 axisX = { transform.q.c, transform.q.s };
    b2Vec2 axisY = { -transform.q.s, transform.q.c };
    b2Pos px = { p.x + 0.5f * axisX.x, p.y + 0.5f * axisX.y };
    b2Pos py = { p.x + 0.5f * axisY.x, p.y + 0.5f * axisY.y };

    MyDrawLine(p, px, 0xFF0000, context); // X axis Red
    MyDrawLine(p, py, 0x00FF00, context); // Y axis Green
}

static void MyDrawPoint(b2Pos p, float size, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    POINT pt = WorldToScreen(p);
    HBRUSH hBrush = CreateSolidBrush(HexToRGB(color));
    RECT rc = { pt.x - 2, pt.y - 2, pt.x + 2, pt.y + 2 };
    FillRect(hdc, &rc, hBrush);
    DeleteObject(hBrush);
}

static void MyDrawString(b2Pos p, const char* s, b2HexColor color, void* context)
{
    HDC hdc = (HDC)context;
    POINT pt = WorldToScreen(p);
    SetTextColor(hdc, HexToRGB(color));
    SetBkMode(hdc, TRANSPARENT);
    TextOutA(hdc, pt.x, pt.y, s, (int)strlen(s));
}

// Scene Creation Helpers
static void CreateGround(b2WorldId worldId)
{
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.position = (b2Vec2){ 0.0f, -0.5f };
    b2BodyId groundId = b2CreateBody(worldId, &bodyDef);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.material.friction = 0.6f;
    shapeDef.material.restitution = 0.2f;

    b2Polygon box = b2MakeBox(30.0f, 0.5f);
    b2CreatePolygonShape(groundId, &shapeDef, &box);

    // Left and right containment walls
    b2Polygon leftWall = b2MakeOffsetBox(0.5f, 20.0f, (b2Vec2){ -22.0f, 15.0f }, (b2Rot){1.0f, 0.0f});
    b2CreatePolygonShape(groundId, &shapeDef, &leftWall);

    b2Polygon rightWall = b2MakeOffsetBox(0.5f, 20.0f, (b2Vec2){ 22.0f, 15.0f }, (b2Rot){1.0f, 0.0f});
    b2CreatePolygonShape(groundId, &shapeDef, &rightWall);
}

// Scene 1: Pyramid of Crates
static void InitScene1(b2WorldId worldId)
{
    CreateGround(worldId);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 2.0f;
    shapeDef.material.friction = 0.6f;
    shapeDef.material.restitution = 0.1f;

    float boxSize = 0.8f;
    float half = boxSize * 0.5f;
    b2Polygon box = b2MakeBox(half, half);

    int rows = 10;
    for (int i = 0; i < rows; ++i)
    {
        float y = half + (float)i * boxSize;
        int count = rows - i;
        float startX = -((float)count - 1.0f) * half;

        for (int j = 0; j < count; ++j)
        {
            float x = startX + (float)j * boxSize;
            b2BodyDef bodyDef = b2DefaultBodyDef();
            bodyDef.type = b2_dynamicBody;
            bodyDef.position = (b2Vec2){ x, y };
            b2BodyId bodyId = b2CreateBody(worldId, &bodyDef);
            b2CreatePolygonShape(bodyId, &shapeDef, &box);
            g_bodyCount++;
        }
    }
}

// Scene 2: Car with Spring Suspension
static void InitScene2(b2WorldId worldId)
{
    CreateGround(worldId);

    // Add terrain bumps
    b2BodyDef groundDef = b2DefaultBodyDef();
    groundDef.position = (b2Vec2){ 0.0f, 0.0f };
    b2BodyId terrainId = b2CreateBody(worldId, &groundDef);
    b2ShapeDef tShapeDef = b2DefaultShapeDef();
    tShapeDef.material.friction = 0.8f;

    for (int i = -15; i <= 15; i += 4)
    {
        b2Polygon bump = b2MakeOffsetBox(1.5f, 0.4f, (b2Vec2){ (float)i, 0.3f }, (b2Rot){ 0.96f, 0.28f });
        b2CreatePolygonShape(terrainId, &tShapeDef, &bump);
    }

    // Car Chassis
    b2BodyDef carDef = b2DefaultBodyDef();
    carDef.type = b2_dynamicBody;
    carDef.position = (b2Vec2){ -12.0f, 4.0f };
    g_carChassis = b2CreateBody(worldId, &carDef);
    g_bodyCount++;

    b2ShapeDef carShapeDef = b2DefaultShapeDef();
    carShapeDef.density = 3.0f;
    carShapeDef.material.friction = 0.5f;
    b2Polygon chassisBox = b2MakeBox(2.2f, 0.5f);
    b2CreatePolygonShape(g_carChassis, &carShapeDef, &chassisBox);

    // Roof
    b2Polygon roofBox = b2MakeOffsetBox(1.2f, 0.4f, (b2Vec2){ -0.3f, 0.8f }, (b2Rot){ 1.0f, 0.0f });
    b2CreatePolygonShape(g_carChassis, &carShapeDef, &roofBox);

    // Wheels
    float wheelRadius = 0.55f;
    b2ShapeDef wheelShapeDef = b2DefaultShapeDef();
    wheelShapeDef.density = 2.0f;
    wheelShapeDef.material.friction = 0.9f;
    b2Circle wheelCircle = { .center = { 0.0f, 0.0f }, .radius = wheelRadius };

    // Front Wheel
    b2BodyDef wheelDef1 = b2DefaultBodyDef();
    wheelDef1.type = b2_dynamicBody;
    wheelDef1.position = (b2Vec2){ -10.5f, 3.2f };
    b2BodyId wheel1 = b2CreateBody(worldId, &wheelDef1);
    b2CreateCircleShape(wheel1, &wheelShapeDef, &wheelCircle);
    g_bodyCount++;

    // Rear Wheel
    b2BodyDef wheelDef2 = b2DefaultBodyDef();
    wheelDef2.type = b2_dynamicBody;
    wheelDef2.position = (b2Vec2){ -13.5f, 3.2f };
    b2BodyId wheel2 = b2CreateBody(worldId, &wheelDef2);
    b2CreateCircleShape(wheel2, &wheelShapeDef, &wheelCircle);
    g_bodyCount++;

    // Wheel Joints (Suspension)
    b2WheelJointDef wjDef1 = b2DefaultWheelJointDef();
    wjDef1.base.bodyIdA = g_carChassis;
    wjDef1.base.bodyIdB = wheel1;
    wjDef1.base.localFrameA.p = (b2Vec2){ 1.5f, -0.6f };
    wjDef1.base.localFrameA.q = (b2Rot){ 1.0f, 0.0f };
    wjDef1.base.localFrameB.p = (b2Vec2){ 0.0f, 0.0f };
    wjDef1.base.localFrameB.q = (b2Rot){ 1.0f, 0.0f };
    wjDef1.enableSpring = true;
    wjDef1.hertz = 4.0f;
    wjDef1.dampingRatio = 0.7f;
    wjDef1.enableMotor = true;
    wjDef1.motorSpeed = -15.0f;
    wjDef1.maxMotorTorque = 60.0f;
    b2CreateWheelJoint(worldId, &wjDef1);
    g_jointCount++;

    b2WheelJointDef wjDef2 = b2DefaultWheelJointDef();
    wjDef2.base.bodyIdA = g_carChassis;
    wjDef2.base.bodyIdB = wheel2;
    wjDef2.base.localFrameA.p = (b2Vec2){ -1.5f, -0.6f };
    wjDef2.base.localFrameA.q = (b2Rot){ 1.0f, 0.0f };
    wjDef2.base.localFrameB.p = (b2Vec2){ 0.0f, 0.0f };
    wjDef2.base.localFrameB.q = (b2Rot){ 1.0f, 0.0f };
    wjDef2.enableSpring = true;
    wjDef2.hertz = 4.0f;
    wjDef2.dampingRatio = 0.7f;
    wjDef2.enableMotor = true;
    wjDef2.motorSpeed = -15.0f;
    wjDef2.maxMotorTorque = 60.0f;
    b2CreateWheelJoint(worldId, &wjDef2);
    g_jointCount++;
}

// Scene 3: Suspension Bridge with Ragdolls
static void InitScene3(b2WorldId worldId)
{
    CreateGround(worldId);

    // Bridge
    int plankCount = 22;
    float plankW = 1.4f;
    float plankH = 0.2f;
    b2Polygon plankBox = b2MakeBox(plankW * 0.5f, plankH * 0.5f);

    b2ShapeDef pShapeDef = b2DefaultShapeDef();
    pShapeDef.density = 1.0f;
    pShapeDef.material.friction = 0.6f;

    b2BodyId prevBody = { 0 };
    b2BodyDef anchorDef = b2DefaultBodyDef();
    anchorDef.position = (b2Vec2){ -16.0f, 10.0f };
    b2BodyId leftAnchor = b2CreateBody(worldId, &anchorDef);
    prevBody = leftAnchor;

    b2BodyId rightAnchor;
    anchorDef.position = (b2Vec2){ 16.0f, 10.0f };
    rightAnchor = b2CreateBody(worldId, &anchorDef);

    float startX = -14.5f;
    for (int i = 0; i < plankCount; ++i)
    {
        b2BodyDef bDef = b2DefaultBodyDef();
        bDef.type = b2_dynamicBody;
        bDef.position = (b2Vec2){ startX + (float)i * 1.35f, 10.0f };
        b2BodyId plank = b2CreateBody(worldId, &bDef);
        b2CreatePolygonShape(plank, &pShapeDef, &plankBox);
        g_bodyCount++;

        b2RevoluteJointDef rjDef = b2DefaultRevoluteJointDef();
        rjDef.base.bodyIdA = prevBody;
        rjDef.base.bodyIdB = plank;
        rjDef.base.localFrameA.p = (i == 0) ? (b2Vec2){ 0.0f, 0.0f } : (b2Vec2){ plankW * 0.5f, 0.0f };
        rjDef.base.localFrameB.p = (b2Vec2){ -plankW * 0.5f, 0.0f };
        b2CreateRevoluteJoint(worldId, &rjDef);
        g_jointCount++;
        prevBody = plank;
    }

    // Connect to right anchor
    b2RevoluteJointDef rjEnd = b2DefaultRevoluteJointDef();
    rjEnd.base.bodyIdA = prevBody;
    rjEnd.base.bodyIdB = rightAnchor;
    rjEnd.base.localFrameA.p = (b2Vec2){ plankW * 0.5f, 0.0f };
    rjEnd.base.localFrameB.p = (b2Vec2){ 0.0f, 0.0f };
    b2CreateRevoluteJoint(worldId, &rjEnd);
    g_jointCount++;

    // Drop Ragdolls
    for (int r = 0; r < 3; ++r)
    {
        float ox = -6.0f + (float)r * 6.0f;
        float oy = 15.0f + (float)r * 2.0f;

        // Torso
        b2BodyDef torsoDef = b2DefaultBodyDef();
        torsoDef.type = b2_dynamicBody;
        torsoDef.position = (b2Vec2){ ox, oy };
        b2BodyId torso = b2CreateBody(worldId, &torsoDef);
        b2ShapeDef sDef = b2DefaultShapeDef();
        sDef.density = 2.0f;
        sDef.material.friction = 0.4f;
        b2Polygon torsoBox = b2MakeBox(0.4f, 0.8f);
        b2CreatePolygonShape(torso, &sDef, &torsoBox);
        g_bodyCount++;

        // Head
        b2BodyDef headDef = b2DefaultBodyDef();
        headDef.type = b2_dynamicBody;
        headDef.position = (b2Vec2){ ox, oy + 1.2f };
        b2BodyId head = b2CreateBody(worldId, &headDef);
        b2Circle headCircle = { .center = { 0.0f, 0.0f }, .radius = 0.35f };
        b2CreateCircleShape(head, &sDef, &headCircle);
        g_bodyCount++;

        b2RevoluteJointDef neck = b2DefaultRevoluteJointDef();
        neck.base.bodyIdA = torso;
        neck.base.bodyIdB = head;
        neck.base.localFrameA.p = (b2Vec2){ 0.0f, 0.8f };
        neck.base.localFrameB.p = (b2Vec2){ 0.0f, -0.35f };
        b2CreateRevoluteJoint(worldId, &neck);
        g_jointCount++;

        // Left Leg
        b2BodyDef legLDef = b2DefaultBodyDef();
        legLDef.type = b2_dynamicBody;
        legLDef.position = (b2Vec2){ ox - 0.25f, oy - 1.2f };
        b2BodyId legL = b2CreateBody(worldId, &legLDef);
        b2Polygon legBox = b2MakeBox(0.18f, 0.5f);
        b2CreatePolygonShape(legL, &sDef, &legBox);
        g_bodyCount++;

        b2RevoluteJointDef hipL = b2DefaultRevoluteJointDef();
        hipL.base.bodyIdA = torso;
        hipL.base.bodyIdB = legL;
        hipL.base.localFrameA.p = (b2Vec2){ -0.25f, -0.8f };
        hipL.base.localFrameB.p = (b2Vec2){ 0.0f, 0.5f };
        b2CreateRevoluteJoint(worldId, &hipL);
        g_jointCount++;

        // Right Leg
        b2BodyDef legRDef = b2DefaultBodyDef();
        legRDef.type = b2_dynamicBody;
        legRDef.position = (b2Vec2){ ox + 0.25f, oy - 1.2f };
        b2BodyId legR = b2CreateBody(worldId, &legRDef);
        b2CreatePolygonShape(legR, &sDef, &legBox);
        g_bodyCount++;

        b2RevoluteJointDef hipR = b2DefaultRevoluteJointDef();
        hipR.base.bodyIdA = torso;
        hipR.base.bodyIdB = legR;
        hipR.base.localFrameA.p = (b2Vec2){ 0.25f, -0.8f };
        hipR.base.localFrameB.p = (b2Vec2){ 0.0f, 0.5f };
        b2CreateRevoluteJoint(worldId, &hipR);
        g_jointCount++;
    }
}

// Scene 4: Rotating Tumbler Drum
static void InitScene4(b2WorldId worldId)
{
    CreateGround(worldId);

    // Center pivot
    b2BodyDef centerDef = b2DefaultBodyDef();
    centerDef.position = (b2Vec2){ 0.0f, 10.0f };
    b2BodyId centerId = b2CreateBody(worldId, &centerDef);

    // Tumbler dynamic frame
    b2BodyDef drumDef = b2DefaultBodyDef();
    drumDef.type = b2_dynamicBody;
    drumDef.position = (b2Vec2){ 0.0f, 10.0f };
    b2BodyId drumId = b2CreateBody(worldId, &drumDef);
    g_bodyCount++;

    b2ShapeDef wallShape = b2DefaultShapeDef();
    wallShape.material.friction = 0.4f;
    wallShape.material.restitution = 0.5f;

    // Build 8-sided hollow drum
    int sides = 8;
    float drumR = 6.0f;
    float wallLen = drumR * sinf(3.14159265f / (float)sides) * 2.0f;
    for (int i = 0; i < sides; ++i)
    {
        float angle = (float)i * (2.0f * 3.14159265f / (float)sides);
        b2Vec2 pos = { drumR * cosf(angle), drumR * sinf(angle) };
        b2Rot rot = { cosf(angle + 1.57079f), sinf(angle + 1.57079f) };
        b2Polygon wall = b2MakeOffsetBox(wallLen * 0.55f, 0.35f, pos, rot);
        b2CreatePolygonShape(drumId, &wallShape, &wall);
    }

    // Motor joint driving the drum
    b2RevoluteJointDef rjDef = b2DefaultRevoluteJointDef();
    rjDef.base.bodyIdA = centerId;
    rjDef.base.bodyIdB = drumId;
    rjDef.enableMotor = true;
    rjDef.motorSpeed = 0.8f;
    rjDef.maxMotorTorque = 10000.0f;
    b2CreateRevoluteJoint(worldId, &rjDef);
    g_jointCount++;

    // Inside particles (bouncy balls and small boxes)
    b2ShapeDef particleDef = b2DefaultShapeDef();
    particleDef.density = 1.0f;
    particleDef.material.friction = 0.2f;
    particleDef.material.restitution = 0.7f;

    for (int i = 0; i < 40; ++i)
    {
        b2BodyDef pDef = b2DefaultBodyDef();
        pDef.type = b2_dynamicBody;
        float rx = -2.5f + (float)(rand() % 50) * 0.1f;
        float ry = 8.0f + (float)(rand() % 40) * 0.1f;
        pDef.position = (b2Vec2){ rx, ry };
        b2BodyId pId = b2CreateBody(worldId, &pDef);

        if (i % 2 == 0)
        {
            b2Circle c = { .center = { 0.0f, 0.0f }, .radius = 0.35f };
            b2CreateCircleShape(pId, &particleDef, &c);
        }
        else
        {
            b2Polygon b = b2MakeBox(0.3f, 0.3f);
            b2CreatePolygonShape(pId, &particleDef, &b);
        }
        g_bodyCount++;
    }
}

static void LoadScene(int scene)
{
    if (b2World_IsValid(g_worldId))
    {
        b2DestroyWorld(g_worldId);
    }

    g_currentScene = scene;
    g_bodyCount = 0;
    g_jointCount = 0;
    g_isDragging = false;

    b2WorldDef worldDef = b2DefaultWorldDef();
    worldDef.gravity = (b2Vec2){ 0.0f, -10.0f };
    g_worldId = b2CreateWorld(&worldDef);

    switch (scene)
    {
    case 1: InitScene1(g_worldId); break;
    case 2: InitScene2(g_worldId); break;
    case 3: InitScene3(g_worldId); break;
    case 4: InitScene4(g_worldId); break;
    }
}

// Mouse Query Callback
static bool OverlapQueryCallback(b2ShapeId shapeId, void* context)
{
    b2BodyId bodyId = b2Shape_GetBody(shapeId);
    if (b2Body_GetType(bodyId) == b2_dynamicBody)
    {
        *(b2BodyId*)context = bodyId;
        return false; // Found body, stop
    }
    return true;
}

static void SpawnRandomBody(b2Vec2 pos)
{
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = pos;
    b2BodyId bodyId = b2CreateBody(g_worldId, &bodyDef);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 2.0f;
    shapeDef.material.friction = 0.5f;
    shapeDef.material.restitution = 0.4f;

    if (rand() % 2 == 0)
    {
        b2Polygon box = b2MakeBox(0.4f + (float)(rand() % 4) * 0.1f, 0.4f + (float)(rand() % 4) * 0.1f);
        b2CreatePolygonShape(bodyId, &shapeDef, &box);
    }
    else
    {
        b2Circle circle = { .center = { 0.0f, 0.0f }, .radius = 0.35f + (float)(rand() % 3) * 0.1f };
        b2CreateCircleShape(bodyId, &shapeDef, &circle);
    }
    g_bodyCount++;
}

static void FireCannonball(b2Vec2 target)
{
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = (b2Vec2){ -20.0f, 12.0f };
    b2BodyId cannonball = b2CreateBody(g_worldId, &bodyDef);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.density = 25.0f; // Heavy iron ball!
    shapeDef.material.friction = 0.3f;
    shapeDef.material.restitution = 0.2f;

    b2Circle ball = { .center = { 0.0f, 0.0f }, .radius = 0.75f };
    b2CreateCircleShape(cannonball, &shapeDef, &ball);

    b2Vec2 dir = { target.x - (-20.0f), target.y - 12.0f };
    float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
    if (len > 0.001f)
    {
        float speed = 35.0f;
        b2Vec2 vel = { (dir.x / len) * speed, (dir.y / len) * speed };
        b2Body_SetLinearVelocity(cannonball, vel);
    }
    g_bodyCount++;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        SetTimer(hWnd, 1, 16, NULL); // ~60 FPS
        LoadScene(1);
        return 0;

    case WM_TIMER:
        if (!g_paused && b2World_IsValid(g_worldId))
        {
            b2World_Step(g_worldId, 1.0f / 60.0f, 4);
        }
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        // Double-buffered GDI
        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBM = CreateCompatibleBitmap(hdc, WINDOW_WIDTH, WINDOW_HEIGHT);
        HGDIOBJ oldBM = SelectObject(memDC, memBM);

        // Fill background (Slate dark)
        HBRUSH bgBrush = CreateSolidBrush(RGB(28, 30, 36));
        RECT clientRc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
        FillRect(memDC, &clientRc, bgBrush);
        DeleteObject(bgBrush);

        // Draw grid lines
        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(42, 45, 54));
        HGDIOBJ oldPen = SelectObject(memDC, gridPen);
        for (float gx = -25.0f; gx <= 25.0f; gx += 5.0f)
        {
            POINT p1 = WorldToScreen((b2Vec2){ gx, -5.0f });
            POINT p2 = WorldToScreen((b2Vec2){ gx, 30.0f });
            MoveToEx(memDC, p1.x, p1.y, NULL);
            LineTo(memDC, p2.x, p2.y);
        }
        for (float gy = 0.0f; gy <= 30.0f; gy += 5.0f)
        {
            POINT p1 = WorldToScreen((b2Vec2){ -25.0f, gy });
            POINT p2 = WorldToScreen((b2Vec2){ 25.0f, gy });
            MoveToEx(memDC, p1.x, p1.y, NULL);
            LineTo(memDC, p2.x, p2.y);
        }
        SelectObject(memDC, oldPen);
        DeleteObject(gridPen);

        // Box2D Debug Draw
        if (b2World_IsValid(g_worldId))
        {
            b2DebugDraw draw = b2DefaultDebugDraw();
            draw.context = (void*)memDC;
            draw.drawingBounds = (b2AABB){ .lowerBound = {-50.0f, -50.0f}, .upperBound = {50.0f, 50.0f} };
            draw.drawShapes = true;
            draw.drawJoints = true;

            draw.DrawSolidPolygonFcn = MyDrawSolidPolygon;
            draw.DrawPolygonFcn = MyDrawPolygon;
            draw.DrawSolidCircleFcn = MyDrawSolidCircle;
            draw.DrawCircleFcn = MyDrawCircle;
            draw.DrawSolidCapsuleFcn = MyDrawSolidCapsule;
            draw.DrawLineFcn = MyDrawLine;
            draw.DrawTransformFcn = MyDrawTransform;
            draw.DrawPointFcn = MyDrawPoint;
            draw.DrawStringFcn = MyDrawString;

            b2World_Draw(g_worldId, &draw);
        }

        // Draw drag line if dragging
        if (g_isDragging && b2Body_IsValid(g_dragBody))
        {
            POINT mousePt;
            GetCursorPos(&mousePt);
            ScreenToClient(hWnd, &mousePt);
            b2Pos bPos = b2Body_GetPosition(g_dragBody);
            POINT bPt = WorldToScreen((b2Vec2){ (float)bPos.x, (float)bPos.y });

            HPEN dragPen = CreatePen(PS_SOLID, 2, RGB(255, 230, 0));
            HGDIOBJ oldDragPen = SelectObject(memDC, dragPen);
            MoveToEx(memDC, bPt.x, bPt.y, NULL);
            LineTo(memDC, mousePt.x, mousePt.y);
            SelectObject(memDC, oldDragPen);
            DeleteObject(dragPen);
        }

        // HUD Text Overlay
        SetBkMode(memDC, TRANSPARENT);
        SetTextColor(memDC, RGB(220, 230, 245));

        const char* sceneNames[] = {
            "Scene 1: Pyramid & Dominoes Collapse",
            "Scene 2: Car with Suspension on Bumpy Terrain",
            "Scene 3: Suspension Bridge with Ragdolls",
            "Scene 4: Rotating Tumbler Drum & Particles"
        };

        char hudBuf[256];
        wsprintfA(hudBuf, "Box2D 3.x Physics Sandbox [MemLLVM Pure Flat Binary]");
        TextOutA(memDC, 16, 14, hudBuf, (int)strlen(hudBuf));

        wsprintfA(hudBuf, "%s | Bodies: %d | Joints: %d %s",
                  sceneNames[g_currentScene - 1], g_bodyCount, g_jointCount,
                  g_paused ? " [PAUSED]" : "");
        SetTextColor(memDC, RGB(100, 210, 255));
        TextOutA(memDC, 16, 36, hudBuf, (int)strlen(hudBuf));

        SetTextColor(memDC, RGB(160, 175, 190));
        wsprintfA(hudBuf, "[1-4]: Switch Scene | [L-Click Drag]: Grasp Body | [R-Click]: Spawn | [Space]: Cannonball | [R]: Reset | [P]: Pause");
        TextOutA(memDC, 16, 58, hudBuf, (int)strlen(hudBuf));

        // Blit to screen
        BitBlt(hdc, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBM);
        DeleteObject(memBM);
        DeleteDC(memDC);

        EndPaint(hWnd, &ps);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        int sx = LOWORD(lParam);
        int sy = HIWORD(lParam);
        b2Vec2 wPos = ScreenToWorld(sx, sy);

        b2AABB aabb;
        aabb.lowerBound = (b2Vec2){ wPos.x - 0.6f, wPos.y - 0.6f };
        aabb.upperBound = (b2Vec2){ wPos.x + 0.6f, wPos.y + 0.6f };
        b2QueryFilter filter = b2DefaultQueryFilter();

        b2BodyId found = { 0 };
        b2World_OverlapAABB(g_worldId, (b2Pos){ 0, 0 }, aabb, filter, OverlapQueryCallback, &found);

        if (b2Body_IsValid(found))
        {
            g_dragBody = found;
            g_isDragging = true;
            SetCapture(hWnd);
        }
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (g_isDragging && b2Body_IsValid(g_dragBody))
        {
            int sx = LOWORD(lParam);
            int sy = HIWORD(lParam);
            b2Vec2 wPos = ScreenToWorld(sx, sy);
            b2Pos bPos = b2Body_GetPosition(g_dragBody);

            b2Vec2 targetVel = { (wPos.x - (float)bPos.x) * 14.0f, (wPos.y - (float)bPos.y) * 14.0f };
            b2Body_SetLinearVelocity(g_dragBody, targetVel);
            b2Body_SetAwake(g_dragBody, true);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (g_isDragging)
        {
            g_isDragging = false;
            ReleaseCapture();
        }
        return 0;

    case WM_RBUTTONDOWN:
    {
        int sx = LOWORD(lParam);
        int sy = HIWORD(lParam);
        b2Vec2 wPos = ScreenToWorld(sx, sy);
        SpawnRandomBody(wPos);
        return 0;
    }

    case WM_KEYDOWN:
    {
        if (wParam >= '1' && wParam <= '4')
        {
            LoadScene((int)(wParam - '0'));
        }
        else if (wParam == 'R')
        {
            LoadScene(g_currentScene);
        }
        else if (wParam == 'P')
        {
            g_paused = !g_paused;
        }
        else if (wParam == VK_SPACE)
        {
            POINT mousePt;
            GetCursorPos(&mousePt);
            ScreenToClient(hWnd, &mousePt);
            b2Vec2 wPos = ScreenToWorld(mousePt.x, mousePt.y);
            FireCannonball(wPos);
        }
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Prevent flicker

    case WM_DESTROY:
        KillTimer(hWnd, 1);
        if (b2World_IsValid(g_worldId))
        {
            b2DestroyWorld(g_worldId);
        }
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcA(hWnd, msg, wParam, lParam);
}

int main()
{
    HINSTANCE hInstance = GetModuleHandleA(NULL);

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXA);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.lpszClassName = "Box2D_Sandbox_Class";

    if (!RegisterClassExA(&wc))
    {
        return 1;
    }

    RECT rc = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRect(&rc, style, FALSE);

    HWND hWnd = CreateWindowExA(
        0,
        wc.lpszClassName,
        "Box2D Physics Sandbox [MemLLVM Flat Binary]",
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL,
        NULL,
        hInstance,
        NULL
    );

    if (!hWnd)
    {
        return 2;
    }

    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    return (int)msg.wParam;
}
