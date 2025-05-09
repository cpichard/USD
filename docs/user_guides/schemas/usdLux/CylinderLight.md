% WARNING: THIS FILE IS GENERATED BY genSchemaDocs. DO NOT EDIT.
% Generated: 11:28PM on April 14, 2025


(CylinderLight)=
# CylinderLight

An intrinsic light that emits light outwards from
a cylinder. The cylinder is centered at the origin and has its major axis on the 
X axis. Note that the cylinder does not emit light from the flat end-caps.

Use CylinderLights to simulate tube-shaped fluorescent lights or 
similarly-shaped light sources, linear lights, light panels, and commercial
lighting used in building interiors. 

The following simple example has a CylinderLight positioned near a Sphere and 
Cube, with a length of 3 and a radius of 0.25. The light has been rotated and
the light cone angle adjusted to limit the light spread, resulting in a thin
band of light. 

```{code-block} usda
#usda 1.0
(
    upAxis = "Y"
)

def Scope "Lights"
{
    def CylinderLight "Light1"
    {
        float inputs:length = 3
        float inputs:radius = 0.25

        float inputs:shaping:cone:angle = 45

        color3f inputs:color = (1, 1, 1)
        float inputs:intensity = 20.0
        float3 xformOp:rotateXYZ = (0, 0, 45)
        double3 xformOp:translate = (0, 0, 1)
        uniform token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
    }
}

def Xform "TestGeom"
{
    def Sphere "Sphere1"
    {
        token purpose = "render"
        color3f[] primvars:displayColor = [(1, 1, 1)] (
            interpolation = "constant"
        )    
        double3 xformOp:translate = (0, 0, -2)
        uniform token[] xformOpOrder = ["xformOp:translate"]          
    }

    def Cube "Cube"
    {
        token purpose = "render"
        color3f[] primvars:displayColor = [(1, 1, 1)] (
            interpolation = "constant"
        )    
        double size = 8
        double3 xformOp:translate = (0, 0, -8)
        uniform token[] xformOpOrder = ["xformOp:translate"]          
    }
}
```

Example RenderMan output for this layer:

```{image} lux_cylinder_light.png
:alt: Example CylinderLight
:width: 600px
```

```{contents}
:depth: 2
:local:
:backlinks: none
```

(CylinderLight_properties)=

## Properties

(CylinderLight_inputs:length)=

### inputs:length

**USD type**: `float`

**Fallback value**: `1.0`

The length of the cylinder in the local X axis.


(CylinderLight_inputs:radius)=

### inputs:radius

**USD type**: `float`

**Fallback value**: `0.5`

The radius of the cylinder.


(CylinderLight_light:shaderId)=

### light:shaderId

**USD type**: `token`

**Fallback value**: `CylinderLight`

The shader ID for a CylinderLight. 
USD will also register a Sdr shader node with a "CylinderLight" identifier and 
the source type "USD" to correspond to the light's inputs


(CylinderLight_treatAsLine)=

### treatAsLine

**USD type**: `bool`

**Fallback value**: `False`

This is used as a hint to renderers that this
light can be treated as a "line light", effectively a zero-radius cylinder.
This is useful for renderers that support non-area lighting. Renderers that
only support area lights will ignore this attribute.


(CylinderLight_inheritedproperties_Boundable)=

## Inherited Properties ({ref}`Boundable`)

(CylinderLight_extent)=

### extent

**USD type**: `float3[]`



(CylinderLight_inheritedproperties_Xformable)=

## Inherited Properties ({ref}`Xformable`)

(CylinderLight_xformOpOrder)=

### xformOpOrder

**USD type**: `token[]`



(CylinderLight_inheritedproperties_Imageable)=

## Inherited Properties ({ref}`Imageable`)

(CylinderLight_proxyPrim)=

### proxyPrim

**USD type**: `rel` (relationship)



(CylinderLight_purpose)=

### purpose

**USD type**: `token`

**Fallback value**: `default`



(CylinderLight_visibility)=

### visibility

**USD type**: `token`

**Fallback value**: `inherited`


