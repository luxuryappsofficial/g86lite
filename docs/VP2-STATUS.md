# VP2 status in G86Lite v3

G86Lite v3.0 deliberately ships the independent display/KMS path first.  It
contains no Nouveau runtime dependency and never initializes PGRAPH/PFIFO,
BSP, VP, PDISPLAY, clocks, or power management.

Hardware H.264 decode is **not enabled yet** in this build.  Claiming otherwise
would be misleading: G86 VP2 needs a working G84 PFIFO channel, GPU virtual
memory objects, BSP/VP Xtensa firmware loading, command submission, fences and
a userspace VA-API/VDPAU bridge.  The standard Mesa Nouveau video backend also
assumes Nouveau's channel ABI and therefore cannot simply be pointed at this
standalone DRM node.

The intended independent VP2 design is:

    FFmpeg/VA-API frontend
            |
       g86vp2 userspace
            |
        DRM ioctls
            |
       G86Lite kernel
       /      |      \
    PFIFO    BSP     VP
              \     /
               VRAM

The important safety rule for this particular project is that the VP2 path
must not require PGRAPH.  Buffer clears and synchronization which Mesa's old
NV84 path normally performs through the graphics engine will instead be done
by the CPU before BSP submission.

Until that stack exists and has been tested on the actual G86, use software
video decode on the v3 display driver.  This keeps the first hardware test
limited to the already-working firmware scanout and VRAM writes.
