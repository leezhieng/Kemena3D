// ---------------------------------------------------------------------------
// Kemena3D AngelScript template
//
// Define any of the lifecycle functions below — unused ones can be deleted.
// Host API available to every script:
//   kObject@ getSelf()           — the object this script is attached to
//   float    getDeltaTime()      — seconds since the last frame
//   float    getFixedDeltaTime() — seconds of the fixed (physics) step
//   void     print(string)       — log to the console
//   kVec3                        — value type: x, y, z, +, -, *, length(),
//                                  normalized(), dot(), cross()
//   kObject: getName/setName, getPosition/setPosition, getRotation/setRotation
//            (Euler degrees), getScale/setScale, forward()/right()/up(),
//            rotate(axis, speed), translate(delta), getActive/setActive,
//            getParent()
// ---------------------------------------------------------------------------

// Called once when the object is created, before any Start().
void Awake()
{
}

// Called once on the first frame, after every Awake() has run.
void Start()
{
    print("Hello from " + getSelf().getName());
}

// Called every frame.
void Update()
{
    // Example: spin the object around its Y axis.
    getSelf().rotate(kVec3(0.0f, 1.0f, 0.0f), 45.0f * getDeltaTime());
}

// Called every fixed (physics) step.
void FixedUpdate()
{
}

// Called every frame, after all Update() calls.
void LateUpdate()
{
}

// Called when the script component becomes active.
void OnEnable()
{
}

// Called when the script component becomes inactive.
void OnDisable()
{
}

// Called once when the object or script is destroyed.
void OnDestroy()
{
}

// ---- Physics collision / trigger events -------------------------------------
// Each receives the OTHER object involved in the contact/overlap as "other".
// They fire when a rigid body (or character controller) collides with this
// object's collider, or enters a Trigger-type collider owned by this object.

// Called when a physics body starts colliding with this object.
// void OnCollisionEnter(kObject@ other) { }

// Called every physics step while a body is colliding with this object.
// void OnCollisionStay(kObject@ other) { }

// Called when a physics body stops colliding with this object.
// void OnCollisionExit(kObject@ other) { }

// Called when a dynamic body enters a trigger volume owned by this object.
// void OnTriggerEnter(kObject@ other) { }

// Called every physics step while a body is inside this object's trigger.
// void OnTriggerStay(kObject@ other) { }

// Called when a dynamic body leaves this object's trigger volume.
// void OnTriggerExit(kObject@ other) { }
