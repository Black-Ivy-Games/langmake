# 2D sprite rotation and polygon contacts

September 21, 2026, 0.5.0 source correction: `AssetRenderer` converts positive 2D Z angles to the opposite sign for bx's clockwise SRT convention. Box2D, authored polygon vertices, picking, and collision debug outlines use counterclockwise positive angles. Without this conversion, a rotated sprite turns away from its physical silhouette. The conversion applies to all flat sprites, including nonphysical drop previews. 3D sprite rotation is unchanged.

This corrects rendering, not saves or the physics API. Existing body angles and angular velocities remain valid. Asset colliders must still exclude transparent padding and approximate concavity within the four-fixture/eight-vertex budget. Cosmetic deformation can temporarily differ from the stable body shape.

Matterfall build 27 uses an isolated Windows source build from the existing baseline plus the established drag-scroll change and this renderer correction. Android relinks its qualified native objects with the corrected renderer. Concurrent audio source changes are not included. Provenance and focused captures live in `.cache/matterfall-render27` and `games/matterfall/.langmake/queue-contacts27`.

Verification should include asymmetric objects at positive and negative angles, resting rotated contacts, and a nonphysical drop preview. An axis-aligned silhouette overlay alone cannot detect this sign mismatch. Platform claims must be based on the rebuilt executable/player, not older captures.
