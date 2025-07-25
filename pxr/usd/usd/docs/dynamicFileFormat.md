# Dynamic File Formats {#Usd_Page_DynamicFileFormat}

## Overview {#Usd_DynamicFileFormat_Overview}

A dynamic file format is an SdfFileFormat that allows the contents of its 
layers to be generated dynamically, when
\ref Usd_DynamicFileFormat_DynamicPayloads "included as a payload", 
within the context of the prim in which it is included.
This feature relies on the fact that all layer file paths can optionally have 
file format arguments that are appended to the layer asset path and are passed 
to the file format when the layer is opened and read (see
SdfLayer::GetFileFormatArguments, SdfLayer::CreateIdentifier, 
SdfLayer::SplitIdentifier). Any file format can
use these arguments to guide how it will translate the contents of the 
referenced data file into a valid USD layer. What makes a \a dynamic file format 
unique is that it is able to __compose values of prim fields or attribute
defaults__ to generate the file format arguments within the context where the 
layer will be included. When the values of any of the composed fields or 
attribute defaults change, the prim automatically regenerates the file format 
arguments and creates new layer contents.

## Creating a Dynamic File Format {#Usd_DynamicFileFormat_Creation}

To create a dynamic file format, we first create a plugin library that
implements a new derived subclass of SdfFileFormat, just like we would for any
other custom file format, and add the type to the library's pluginInfo.json. 

For an example in this document, we'll start with a new file format,
MyDynamicFileFormat:

\code
class MyDynamicFileFormat : public SdfFileFormat
{
public:
    // Required SdfFileFormat overrides.
    bool CanRead(const std::string &file) const override;
    bool Read(SdfLayer *layer,
              const std::string& resolvedPath,
              bool metadataOnly) const override;

protected:
    SDF_FILE_FORMAT_FACTORY_ACCESS;

    virtual ~MyDynamicFileFormat();
    MyDynamicFileFormat() : 
        SdfFileFormat(
            TfToken("MyDynamicFileFormat"), // formatId
            TfToken("1.0"), // versionString
            TfToken("usd"), // target
            "mydynamicfile") // extension
    {}
}
\endcode

\code
### plugInfo.json

{
    "Plugins": [
        {
            "Info": {
                "Types": {
                    "MyDynamicFileFormat": {
                        "bases": [
                            "SdfFileFormat"
                        ],
                        "displayName": "Dynamic File Format",
                        "extensions": [
                            "mydynamicfile"
                        ],
                        "formatId": "MyDynamicFileFormat",
                        "primary": true,
                        "target": "usd"
                    }
                }
            },
            "LibraryPath": "@PLUG_INFO_LIBRARY_PATH@",
            "Name": "myDynamicFileFormat",
            "ResourcePath": "@PLUG_INFO_RESOURCE_PATH@",
            "Root": "@PLUG_INFO_ROOT@",
            "Type": "library"
        }
    ]
}
\endcode

An essential piece of implementing a dynamic file format, that will not be
covered in depth here, is using file format arguments that can be appended to 
the layer's file path to generate or alter some portion of the contents of the 
layer when the file is read. This can be done by using the file format 
arguments in the implementation of the Read function or, in the case of a fully 
procedural layer, creating a custom subclass of SdfAbstractData that utilizes 
the arguments. The file format arguments are also part of the identity of the 
layer, meaning layers opened with the same asset path but different arguments
are opened as separate layers. 
Refer to the included \ref Usd_DynamicFileFormat_Examples "examples" for 
detailed methods of creating dynamic content.

Assume that for this example we wrote the implementation of 
MyDynamicFileFormat::Read to use SdfLayer::GetFileFormatArguments to get the 
file format arguments from the asset path of the \p layer it is reading (these 
would be the same \p args that are passed to SdfLayer::FindOrOpen when the layer 
is opened). Also assume our Read function uses the values of the arguments 
\a dynamicName and \a isPositive to alter the contents of the \p layer.

Now to make this file format dynamic, we must also have this class derive
from PcpDynamicFileFormatInterface and implement 
ComposeFieldsForFileFormatArguments, and optionally implement 
CanFieldChangeAffectFileFormatArguments and/or 
CanAttributeDefaultValueChangeAffectFileFormatArguments.

\code
class MyDynamicFileFormat : 
    public SdfFileFormat, public PcpDynamicFileFormatInterface
{
    ...

public:
    // Required PcpDynamicFileFormatInterface overrides
    void ComposeFieldsForFileFormatArguments(
        const std::string& assetPath, 
        const PcpDynamicFileFormatContext& context,
        FileFormatArguments* args,
        VtValue *dependencyContextData) const override;

    // Optional overrides
    bool CanFieldChangeAffectFileFormatArguments(
        const TfToken& field,
        const VtValue& oldValue,
        const VtValue& newValue,
        const VtValue &dependencyContextData) const override;
    bool CanAttributeDefaultValueChangeAffectFileFormatArguments(
        const TfToken &attributeName,
        const VtValue &oldValue,
        const VtValue &newValue,
        const VtValue &dependencyContextData) const override;        

   ...
}
\endcode

The default implementation of CanFieldChangeAffectFileFormatArguments returns
true, indicating that any value change for a passed in field will require file 
format arguments to be recomputed. Derived classes should override this function
if there are scenarios where field value changes do not need to recompute 
arguments. This will reduce the number of unnecessary recompositions of dynamic 
payloads. See \ref Usd_DynamicFileFormat_ChangeManagement for an
explanation of how this function is used and implemented.

The default implementation of CanAttributeDefaultValueChangeAffectFileFormatArguments
also returns true. This method is similar in concept to 
CanFieldChangeAffectFileFormatArguments, but used for situations where changes 
to an attribute's default value will require recomputing file format
arguments. Derived classes should override this function
if there are scenarios where attribute default changes do not need to recompute 
arguments.

Now, because this file format implements PcpDynamicFileFormatInterface,
\ref PcpDynamicFileFormatInterface::ComposeFieldsForFileFormatArguments "ComposeFieldsForFileFormatArguments"
will be called while composing the prim
whenever a prim spec includes a payload to a file with the extension 
".mydynamicfile". This is called before the file is opened to generate 
additional file format arguments that will be added to the file asset path. 
The function can use the given PcpDynamicFileFormatContext to compose the
value of the strongest opinion of a field or attribute default on the prim being 
indexed at the point in which the payload is being included. Currently the only 
metadata fields that are allowed to be evaluated by the context are  
defined plugin metadata fields, so we'll have to define the fields we plan to 
use in our plugInfo.json. This restriction may be lifted in the future to 
include builtin fields like \a variantSelection but for now those fields cannot 
be used. The context can also be used to evaluate attribute defaults, as 
described in \ref Usd_DynamicFileFormat_Attributes.

So, in our example plugInfo.json, we'll also define some new SdfMetadata fields,
\a dynamicName and \a dynamicNumber that we can use to compute file format 
arguments:

\code
### plugInfo.json

{
    "Plugins": [
        {
            "Info": {
                "SdfMetadata": {
                    "dynamicName": {
                        "type": "string",
                        "default": "",
                        "displayGroup": "Core",
                        "appliesTo": ["prims"],
                        "documentation:": "Example custom string metadata."
                    },
                    "dynamicNumber": {
                        "type": "int",
                        "default": 1,
                        "displayGroup": "Core",
                        "appliesTo": ["prims"],
                        "documentation:": "Example custom number metadata"
                    }
                },

                ...
            },

            ...
        }
    ]
}
\endcode

In our 
\ref PcpDynamicFileFormatInterface::ComposeFieldsForFileFormatArguments "ComposeFieldsForFileFormatArguments"
implementation we'll use the \p context to compose the strongest value of the
\a dynamicName and \a dynamicNumber metadata fields to generate file format
arguments to add to \p args: 

\code
void MyDynamicFileFormat::ComposeFieldsForFileFormatArguments(
    const std::string& assetPath, 
    const PcpDynamicFileFormatContext& context,
    FileFormatArguments* args,
    VtValue *dependencyContextData) const
{
    static const TfToken dynamicNameToken("dynamicName");
    VtValue dynamicNameValue;
    if (context.ComposeValue(dynamicNameToken, &dynamicNameValue)) {
        (*args)[dynamicNameToken] = TfStringify(dynamicNameValue);
    }

    static const TfToken dynamicNumberToken("dynamicNumber");
    static const TfToken isPositiveToken("isPositive");
    VtValue dynamicNumberValue;
    if (context.ComposeValue(dynamicNumberToken, &dynamicNumberValue)) {
        if (dynamicNumberValue.IsHolding<int>() &&
            dynamicNumberValue.UncheckedGet<int>() > 0) {
            (*args)[isPositiveToken] = "true";
        } else {
            (*args)[isPositiveToken] = "false";
        }
    }
}
\endcode

For \a dynamicName, we add its computed string value into the \p args with the
key \a dynamicName. However it's not necessary to always directly transpose 
field values into \p args using the field or attribute name as the key. For 
\a dynamicNumber we compute its composed value to check if it is a positive 
integer and write either "true" or "false" into \p args with the key \a 
isPositive instead.

For an example implementation of ComposeFieldsForFileFormatArguments that
composes attribute defaults for \a dynamicName and \a dynamicNumber, see 
\ref Usd_DynamicFileFormat_Attributes. Note that you should avoid composing 
both field values and attribute defaults for computing the same file format 
argument in your ComposeFieldsForFileFormatArguments implementation.

With the plugin complete, here's how the dynamic file format would work in 
practice. Let's say we have the following usd file:

\code
### root.usd

#usda 1.0

def "Root" (
    references = </Params>
    payload = @./dynamic.mydynamicfile@
)
{
}

def "Params" (
    dynamicName = "Foo"
    dynamicNumber = 8
)
{
}
\endcode 

The prim \a Root has a reference to the \a Params prim which has value opinions
for the plugin fields \a dynamicName and \a dynamicNumber. \a Root also has a 
payload to a file with the ".mydynamicfile" extension. When the prim index is 
computed for \a Root, and the indexer gets to composing the payload, it will see
that file format is \a MyDynamicFileFormat and it will call the format's 
ComposeFieldsForFileFormatArguments function to produce the file format arguments. 
At this point in composition, the \p context includes the reference to \a Params 
and will get its values for \a dynamicName and \a dynamicNumber as those fields'
strongest opinions to produce the fileformat arguments: 
\li \a dynamicName = "Foo"
\li \a isPositive = "true"

These args are added to the asset path of the payload layer that will be read
giving the resolved layer path:
\n\b dynamic.mydynamicfile:SDF_FORMAT_ARGS:dynamicName=Foo:isPositive=true

As mentioned above MyDynamicFileFormat's Read function uses these arguments to
generate the identity and contents of the layer. Now say we update root.usd and 
add the \a dynamicName field to \a Root with the value "Bar":

\code
### root.usd

#usda 1.0

def "Root" (
    dynamicName = "Bar"
    references = </Params>
    payload = @./dynamic.mydynamicfile@
)
{
}

def "Params" (
    dynamicName = "Foo"
    dynamicNumber = 8
)
{
}
\endcode 

When \a Root is prim composed again, the stongest opinion for \a dynamicName,
in the \p context where payload is composed, will come from \a Root giving us 
the file format arguments:  
\li \a dynamicName = "Bar"
\li \a isPositive = "true"

Note that the strongest opinion for \a dynamicNumber still comes from \a Params.
The resolved payload layer path is now:
\n\b dynamic.mydynamicfile:SDF_FORMAT_ARGS:dynamicName=Bar:isPositive=true

We have new layer with a different identity and contents from the same payload 
field without changing the payload declaration itself.

### Using Attributes To Compute Arguments {#Usd_DynamicFileFormat_Attributes}

Your dynamic file format plugin can also use uniform attribute defaults instead
of metadata fields to compute file format arguments. Unlike metadata fields, 
these attributes do not need to be registered in your plugInfo.json. Only the 
default value of an attribute can be used and the attribute should be declared 
uniform in the USD data.

If you wanted to modify the MyDynamicFileFormat plugin example described earlier
to use attribute defaults instead of fields, you would change the implementation 
of \ref PcpDynamicFileFormatInterface::ComposeFieldsForFileFormatArguments "ComposeFieldsForFileFormatArguments"
to use ComposeAttributeDefaultValue to compose the strongest \a dynamicName 
and \a dynamicNumber attribute defaults. 

\code
void MyDynamicFileFormat::ComposeFieldsForFileFormatArguments(
    const std::string& assetPath, 
    const PcpDynamicFileFormatContext& context,
    FileFormatArguments* args,
    VtValue *dependencyContextData) const
{
    static const TfToken dynamicNameToken("dynamicName");
    VtValue dynamicNameValue;
    if (context.ComposeAttributeDefaultValue(dynamicNameToken, &dynamicNameValue)) {
        (*args)[dynamicNameToken] = TfStringify(dynamicNameValue);
    }

    static const TfToken dynamicNumberToken("dynamicNumber");
    static const TfToken isPositiveToken("isPositive");
    VtValue dynamicNumberValue;
    if (context.ComposeAttributeDefaultValue(dynamicNumberToken, &dynamicNumberValue)) {
        if (dynamicNumberValue.IsHolding<int>() &&
            dynamicNumberValue.UncheckedGet<int>() > 0) {
            (*args)[isPositiveToken] = "true";
        } else {
            (*args)[isPositiveToken] = "false";
        }
    }
}
\endcode

In general, while USD supports composing both field values and attribute 
defaults in your ComposeFieldsForFileFormatArguments implementation, you should 
avoid using both field and attribute defaults in your plugin if possible.

If you want to filter out attribute default changes that would not require 
recomputing the file format arguments, you can implement 
CanAttributeDefaultValueChangeAffectFileFormatArguments and add your logic
for determining which default value changes require recomputing the appropriate
argument.

USD data that would use the uniform attribute defaults for this plugin 
would look like:

\code
### root.usd

#usda 1.0

def "Root" (
    references = </Params>
    payload = @./dynamic.mydynamicfile@
)
{
}

def "Params" (
)
{
    uniform string dynamicName = "Foo"
    uniform int dynamicNumber = 8
}
\endcode 

## Advanced Examples {#Usd_DynamicFileFormat_Examples}

We include two examples of dynamic file format plugins in pxr/extras/usd/examples.
One ot the major differences between these examples that's worth highlighting is
how the scene description is represented. SdfAbstractData is the base class for 
all scene description represented by a layer and we have a choice when writing a
file format as to whether we want use the default SdfData class for our scene 
description or if we want to write our own custom data representation.
\li \b usdRecursivePayloadsExample - This example uses file format arguments to
recursively generate prims with payloads targeting the same file but with 
a different set of arguments. It uses the default SdfData representation 
provided by SdfFileFormat::InitData, just like the text based usda 
file format, and creates prim specs in its Read function through the standard 
SdfPrimSpec API. The generated scene description is pretty simple and minimal 
so it doesn't warrant the complexity of a custom SdfAbstractData type. 
\li \b usdDancingCubesExample - This example generates a cube made up of 
animated cubes backed by a completely procedural scene description 
representation. It implements its own SdfAbstractData subclass that is returned
by overriding SdfFileFormat::InitData. The file format generates a small set 
of parameters from the file format arguments and provides them to the data 
implementation of the layer. The SdfAbstractData subclass uses these parameters
to cache some information about the scene and provides the API that generates 
spec data on the fly when requested. This example greatly benefits from a 
customized SdfAbstractData implementation as it avoids having to precompute 
every time sample for every prim when the layer is opened.

## Dynamic Payloads {#Usd_DynamicFileFormat_DynamicPayloads}

As mentioned above, the composition of prim fields or attribute defaults into 
file format arguments only occurs when a dynamic asset is included as a payload. 
We refer to such a payload as a <em>dynamic payload</em>. This behavior is 
intentionally exclusive to payloads, as opposed to references, for a couple of 
reasons:
\li Payloads are the weakest composition arcs that read in layer files. The 
effect of this is that when prim indexing encounters a dynamic payload, the 
context used for composing fields will have access to all local or referenced 
opinions on those fields, giving the most complete context with which to process
the dynamic file's arguments. 
\li Payloads can be loaded and unloaded providing a convenient way to recompute 
dynamic layers whose contents depend on factors other than just file format
arguments alone.

A prim index can have multiple payload arcs with any number of them being 
dynamic. Opinions from stronger payloads are included in the context for weaker 
dynamic payloads when computing file format arguments.

## Dependencies and Change Management {#Usd_DynamicFileFormat_ChangeManagement}

When the PcpDynamicFileFormatContext is used to compute a field value in 
\ref PcpDynamicFileFormatInterface::ComposeFieldsForFileFormatArguments "ComposeFieldsForFileFormatArguments"
using 
\ref PcpDynamicFileFormatContext::ComposeValue "ComposeValue" (or 
\ref PcpDynamicFileFormatContext::ComposeValueStack "ComposeValueStack")
during prim indexing, a dependency is automatically registered for that payload
arc on that field value. This means that change management in Pcp knows which
fields were used to generate file format arguments for the payload's layer and 
therefore may need to invalidate the prim index that includes the payload if
any of those fields change. It is recommended when writing an implementation of
\ref PcpDynamicFileFormatInterface::ComposeFieldsForFileFormatArguments "ComposeFieldsForFileFormatArguments"
to only call ComposeValue on fields as needed if the use of any fields
are conditional as it prevents unnecessary change dependencies on unused fields.
The same guidance applies when using 
\ref PcpDynamicFileFormatContext::ComposeAttributeDefaultValue "ComposeAttributeDefaultValue" 
in your ComposeFieldsForFileFormatArguments to compute attribute defaults.

Since prim indexes that include dynamic payloads automatically have a dependency
on changes to the computed fields, the other interface function 
\ref PcpDynamicFileFormatInterface::CanFieldChangeAffectFileFormatArguments "CanFieldChangeAffectFileFormatArguments"
exists to filter out field changes that we know will not 
alter the file format arguments. Looking at \a MyDynamicFileFormat still, the 
\a dynamicNumber field holds an integer value that is used to populate the 
boolean \a isPositive argument. There are multiple values of \a dynamicNumber 
that produce the same arguments so we can write
\ref PcpDynamicFileFormatInterface::CanFieldChangeAffectFileFormatArguments "CanFieldChangeAffectFileFormatArguments"
to take advantage of this:

\code
bool MyDynamicFileFormat::CanFieldChangeAffectFileFormatArguments(
    const TfToken& field,
    const VtValue& oldValue,
    const VtValue& newValue,
    const VtValue &dependencyContextData) const
{
    static const TfToken dynamicNumberToken("dynamicNumber");
    if (field == dynamicNumberToken) {
        if (oldValue.IsEmpty() != newValue.IsEmpty()) {
            return true;
        }
        const bool oldIsPositive = (oldValue.IsHolding<int>() && 
                                    oldValue.UncheckedGet<int>() > 0);
        const bool newIsPositive = (newValue.IsHolding<int>() && 
                                    newValue.UncheckedGet<int>() > 0);
        return oldIsPositive != newIsPositive;
    }

    return true;
}
\endcode

Here if the field is \a dynamicNumber we check if the old and new values 
would produce the same isPositive argument, and return false if they would, thus
telling Pcp change management that we don't need to invalidate the prim 
index that includes the payload.

There is one more parameter \p dependencyContextData that exists in both 
\ref PcpDynamicFileFormatInterface::ComposeFieldsForFileFormatArguments "ComposeFieldsForFileFormatArguments"
and
\ref PcpDynamicFileFormatInterface::CanFieldChangeAffectFileFormatArguments "CanFieldChangeAffectFileFormatArguments".
This is an arbitrary typed VtValue that can be populated in 
\ref PcpDynamicFileFormatInterface::ComposeFieldsForFileFormatArguments "ComposeFieldsForFileFormatArguments"
if there's specific information that would be helpful in determining if a field
value change is relevant. The \p dependencyContextData is stored and passed back to
\ref PcpDynamicFileFormatInterface::CanFieldChangeAffectFileFormatArguments "CanFieldChangeAffectFileFormatArguments"
when processing a field change within the same prim index context. See
\ref Usd_DynamicFileFormat_Examples "usdRecursivePayloadsExample" for a very 
basic example of how this can be used.
