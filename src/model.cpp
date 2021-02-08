#include "model.hpp"

Model::Model() : animDuration(0.0), currentAnimation(0), bonesCount(0)
{
    scene = nullptr;
}

Model::~Model()
{
    scene = nullptr;
}

void Model::InitFromScene(const aiScene* scene)
{
    this->scene = scene;
    globalInverseTransform = mat4Convert(scene->mRootNode->mTransformation);
    globalInverseTransform = glm::inverse(globalInverseTransform);

    if (scene->HasAnimations() && scene->mAnimations[0]->mTicksPerSecond != 0.0f)
        ticksPerSecond = scene->mAnimations[0]->mTicksPerSecond;
    else
        ticksPerSecond = 25.0f;

    processNode(scene->mRootNode, scene);
}

// draws the model, and thus all its meshes
void Model::Draw(Shader shader)
{
    if (HasAnimations())
        shader.SetInteger("animated", 1);

    for (unsigned int i = 0; i < meshes.size(); i++)
        meshes[i].Draw(shader);

    if (HasAnimations())
        shader.SetInteger("animated", 0);
}

void Model::SetBoneTransformations(Shader shader, GLfloat currentTime)
{
    if (HasAnimations())
    {
        std::vector<glm::mat4> transforms;
        boneTransform((float)currentTime, transforms);
        shader.SetMatrix4v("gBones", transforms);
    }
}

void Model::SetAnimation(unsigned int animation)
{
    if (animation >= 0 && animation < GetNumAnimations())
        currentAnimation = animation;
}

// processes a node in a recursive fashion. Processes each individual mesh located at the node and repeats this process on its children nodes (if any).
void Model::processNode(aiNode* node, const aiScene* scene)
{
    // process each mesh located at the current node
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        // the node object only contains indices to index the actual objects in the scene.
        // the scene contains all the data, node is just to keep stuff organized (like relations between nodes).
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(processMesh(mesh, scene));
    }
    // after we've processed all of the meshes (if any) we then recursively process each of the children nodes
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        processNode(node->mChildren[i], scene);
    }
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene)
{
    // data to fill
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<Texture> textures;

    // Walk through each of the mesh's vertices
    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        glm::vec3 vector; // we declare a placeholder vector since assimp uses its own vector class that doesn't directly convert to glm's vec3 class so we transfer the data to this placeholder glm::vec3 first.
        // positions
        vector.x = mesh->mVertices[i].x;
        vector.y = mesh->mVertices[i].y;
        vector.z = mesh->mVertices[i].z;
        vertex.Position = vector;
        // normals
        if (mesh->HasNormals())
        {
            vector.x = mesh->mNormals[i].x;
            vector.y = mesh->mNormals[i].y;
            vector.z = mesh->mNormals[i].z;
            vertex.Normal = vector;
        }
        // texture coordinates
        if (mesh->mTextureCoords[0]) // does the mesh contain texture coordinates?
        {
            glm::vec2 vec;
            // a vertex can contain up to 8 different texture coordinates. We thus make the assumption that we won't
            // use models where a vertex can have multiple texture coordinates so we always take the first set (0).
            vec.x = mesh->mTextureCoords[0][i].x;
            vec.y = mesh->mTextureCoords[0][i].y;
            vertex.TexCoords = vec;
        }
        else
            vertex.TexCoords = glm::vec2(0.0f, 0.0f);

        // Bone Weights are initialised in next for loop
        vertex.BoneWeights = glm::vec4(0.0f);

        vertices.push_back(vertex);
    }

    // process bones
    for (unsigned int i = 0; i < mesh->mNumBones; i++)
    {
        unsigned int boneIndex = 0;
        std::string boneName(mesh->mBones[i]->mName.data);

        if (boneMapping.find(boneName) == boneMapping.end())
        {
            // allocate an index for the new bone
            boneIndex = bonesCount;
            bonesCount++;
            BoneMatrix boneMatrix;
            boneMatrices.push_back(boneMatrix);

            boneMatrices[boneIndex].BoneOffset = mat4Convert(mesh->mBones[i]->mOffsetMatrix);
            boneMapping[boneName] = boneIndex;
        }
        else
            boneIndex = boneMapping[boneName];

        for (unsigned int j = 0; j < mesh->mBones[i]->mNumWeights; j++)
        {
            unsigned int vertexID = mesh->mBones[i]->mWeights[j].mVertexId;
            float weight = mesh->mBones[i]->mWeights[j].mWeight;

            for (unsigned int g = 0; g < NUM_BONES_PER_VERTEX; g++)
            {
                if (vertices[vertexID].BoneWeights[g] == 0.0)
                {
                    vertices[vertexID].BoneIDs[g] = boneIndex;
                    vertices[vertexID].BoneWeights[g] = weight;
                    break;
                }
            }
        }
    }

    // now wak through each of the mesh's faces (a face is a mesh its triangle) and retrieve the corresponding vertex indices.
    for(unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        // retrieve all indices of the face and store them in the indices vector
        for(unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    // process materials
    aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
    // we assume a convention for sampler names in the shaders. Each diffuse texture should be named
    // as 'texture_diffuseN' where N is a sequential number ranging from 1 to MAX_SAMPLER_NUMBER.
    // Same applies to other texture as the following list summarizes:
    // diffuse: texture_diffuseN
    // specular: texture_specularN
    // normal: texture_normalN
    // emission: texture_emissionN

    // 1. diffuse maps
    std::vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
    textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());
    // 2. specular maps
    std::vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
    textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());
    // 3. normal maps
    std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
    textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());
    // 4. emission maps
    std::vector<Texture> emissionMaps = loadMaterialTextures(material, aiTextureType_EMISSIVE, "texture_emission");
    textures.insert(textures.end(), emissionMaps.begin(), emissionMaps.end());

    // return a mesh object created from the extracted mesh data
    return Mesh(vertices, indices, textures);
}

void Model::boneTransform(float timeInSeconds, std::vector<glm::mat4>& transforms)
{
    glm::mat4 identity = glm::mat4(1.0f);

    // Calculate animation duration
    unsigned int numPosKeys = scene->mAnimations[currentAnimation]->mChannels[0]->mNumPositionKeys;
    animDuration = scene->mAnimations[currentAnimation]->mChannels[0]->mPositionKeys[numPosKeys - 1].mTime;

    float ticksPerSecond = (float)(scene->mAnimations[currentAnimation]->mTicksPerSecond != 0 ? scene->mAnimations[currentAnimation]->mTicksPerSecond : 25.0f);
    float timeInTicks = timeInSeconds * ticksPerSecond;
    float animationTime = fmod(timeInTicks, animDuration);
    readNodeHeirarchy(animationTime, scene->mRootNode, identity);
    transforms.resize(bonesCount);

    for (unsigned int  i = 0; i < bonesCount; i++)
        transforms[i] = boneMatrices[i].FinalTransformation;
}

uint Model::findPosition(float animationTime, const aiNodeAnim* nodeAnim)
{
    for (uint i = 0 ; i < nodeAnim->mNumPositionKeys - 1 ; i++)
    {
        if (animationTime < (float)nodeAnim->mPositionKeys[i + 1].mTime)
            return i;
    }

    assert(0);

    return 0;
}

uint Model::findRotation(float animationTime, const aiNodeAnim* nodeAnim)
{
    assert(nodeAnim->mNumRotationKeys > 0);

    for (uint i = 0 ; i < nodeAnim->mNumRotationKeys - 1 ; i++)
    {
        if (animationTime < (float)nodeAnim->mRotationKeys[i + 1].mTime)
            return i;
    }

    assert(0);

    return 0;
}

uint Model::findScaling(float animationTime, const aiNodeAnim* nodeAnim)
{
    assert(nodeAnim->mNumScalingKeys > 0);

    for (uint i = 0 ; i < nodeAnim->mNumScalingKeys - 1 ; i++)
    {
        if (animationTime < (float)nodeAnim->mScalingKeys[i + 1].mTime)
            return i;
    }

    assert(0);

    return 0;
}

void Model::calcInterpolatedPosition(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim)
{
    if (nodeAnim->mNumPositionKeys == 1)
    {
        out = nodeAnim->mPositionKeys[0].mValue;
        return;
    }

    uint positionIndex = findPosition(animationTime, nodeAnim);
    uint nextPositionIndex = (positionIndex + 1);
    assert(nextPositionIndex < nodeAnim->mNumPositionKeys);
    float deltaTime = (float)(nodeAnim->mPositionKeys[nextPositionIndex].mTime - nodeAnim->mPositionKeys[positionIndex].mTime);
    float factor = (animationTime - (float)nodeAnim->mPositionKeys[positionIndex].mTime) / deltaTime;
    assert(factor >= 0.0f && factor <= 1.0f);
    const aiVector3D& start = nodeAnim->mPositionKeys[positionIndex].mValue;
    const aiVector3D& end = nodeAnim->mPositionKeys[nextPositionIndex].mValue;
    aiVector3D delta = end - start;
    out = start + factor * delta;
}

void Model::calcInterpolatedRotation(aiQuaternion& out, float animationTime, const aiNodeAnim* nodeAnim)
{
    // we need at least two values to interpolate...
    if (nodeAnim->mNumRotationKeys == 1)
    {
        out = nodeAnim->mRotationKeys[0].mValue;
        return;
    }

    uint rotationIndex = findRotation(animationTime, nodeAnim);
    uint nextRotationIndex = (rotationIndex + 1);
    assert(nextRotationIndex < nodeAnim->mNumRotationKeys);
    float deltaTime = (float)(nodeAnim->mRotationKeys[nextRotationIndex].mTime - nodeAnim->mRotationKeys[rotationIndex].mTime);
    float factor = (animationTime - (float)nodeAnim->mRotationKeys[rotationIndex].mTime) / deltaTime;
    assert(factor >= 0.0f && factor <= 1.0f);
    const aiQuaternion& startRotationQ = nodeAnim->mRotationKeys[rotationIndex].mValue;
    const aiQuaternion& endRotationQ   = nodeAnim->mRotationKeys[nextRotationIndex].mValue;
    aiQuaternion::Interpolate(out, startRotationQ, endRotationQ, factor);
    out = out.Normalize();
}

void Model::calcInterpolatedScaling(aiVector3D& out, float animationTime, const aiNodeAnim* nodeAnim)
{
    if (nodeAnim->mNumScalingKeys == 1)
    {
        out = nodeAnim->mScalingKeys[0].mValue;
        return;
    }

    uint scalingIndex = findScaling(animationTime, nodeAnim);
    uint nextScalingIndex = (scalingIndex + 1);
    assert(nextScalingIndex < nodeAnim->mNumScalingKeys);
    float deltaTime = (float)(nodeAnim->mScalingKeys[nextScalingIndex].mTime - nodeAnim->mScalingKeys[scalingIndex].mTime);
    float factor = (animationTime - (float)nodeAnim->mScalingKeys[scalingIndex].mTime) / deltaTime;
    assert(factor >= 0.0f && factor <= 1.0f);
    const aiVector3D& start = nodeAnim->mScalingKeys[scalingIndex].mValue;
    const aiVector3D& end   = nodeAnim->mScalingKeys[nextScalingIndex].mValue;
    aiVector3D delta = end - start;
    out = start + factor * delta;
}

void Model::readNodeHeirarchy(float animationTime, const aiNode* node, const glm::mat4& parentTransform)
{
    std::string nodeName(node->mName.data);

    const aiAnimation* animation = scene->mAnimations[0];

    glm::mat4 nodeTransformation = mat4Convert(node->mTransformation);

    const aiNodeAnim* nodeAnim = findNodeAnim(animation, nodeName);

    if (nodeAnim)
    {
        // Interpolate scaling and generate scaling transformation matrix
        aiVector3D scaling;
        calcInterpolatedScaling(scaling, animationTime, nodeAnim);
        glm::vec3 scale = glm::vec3(scaling.x, scaling.y, scaling.z);
        glm::mat4 scalingM = glm::scale(glm::mat4(1.0f), scale);

        // Interpolate rotation and generate rotation transformation matrix
        aiQuaternion rotationQ;
        calcInterpolatedRotation(rotationQ, animationTime, nodeAnim);
        glm::quat rotate = quatConvert(rotationQ);
        glm::mat4 rotationM = glm::toMat4(rotate);

        // Interpolate translation and generate translation transformation matrix
        aiVector3D translation;
        calcInterpolatedPosition(translation, animationTime, nodeAnim);
        glm::vec3 translate = glm::vec3(translation.x, translation.y, translation.z);
        glm::mat4 translationM = glm::translate(glm::mat4(1.0f), translate);

        // Combine the above transformations
        nodeTransformation = translationM * rotationM * scalingM;
    }

    // Combine with node Transformation with Parent Transformation
    glm::mat4 globalTransformation = parentTransform * nodeTransformation;

    if (boneMapping.find(nodeName) != boneMapping.end())
    {
        uint boneIndex = boneMapping[nodeName];
        boneMatrices[boneIndex].FinalTransformation = globalInverseTransform * globalTransformation * boneMatrices[boneIndex].BoneOffset;
    }

    for (uint i = 0 ; i < node->mNumChildren ; i++)
        readNodeHeirarchy(animationTime, node->mChildren[i], globalTransformation);
}

const aiNodeAnim* Model::findNodeAnim(const aiAnimation* animation, const std::string nodeName)
{
    for (uint i = 0 ; i < animation->mNumChannels ; i++) {
        const aiNodeAnim* nodeAnim = animation->mChannels[i];

        if (std::string(nodeAnim->mNodeName.data) == nodeName)
            return nodeAnim;
    }

    return NULL;
}

// checks all material textures of a given type and loads the textures if they're not loaded yet.
// the required info is returned as a Texture struct.
std::vector<Texture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName)
{
    std::vector<Texture> textures;
    for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
    {
        aiString str;
        mat->GetTexture(type, i, &str);
        // check if texture was loaded before and if so, continue to next iteration: skip loading a new texture
        bool skip = false;
        for (unsigned int j = 0; j < loadedTextures.size(); j++)
        {
            if (strcmp(loadedTextures[j].Path.data(), str.C_Str()) == 0)
            {
                textures.push_back(loadedTextures[j]);
                skip = true; // a texture with the same filepath has already been loaded, continue to next one. (optimization)
                break;
            }
        }
        if (!skip)
        {   // if texture hasn't been loaded already, load it
            Texture texture;
            texture.ID = TextureFromFile(str.C_Str(), this->directory);
            texture.Type = typeName;
            texture.Path = str.C_Str();
            textures.push_back(texture);
            loadedTextures.push_back(texture); // store it as texture loaded for entire model, to ensure we won't unnecesery load duplicate textures.
        }
    }
    return textures;
}

unsigned int TextureFromFile(const char* filename, const std::string& directory, bool /* gamma */)
{
    std::string path = std::string(filename);
    path = directory + '/' + path;

    unsigned int textureID;
    glGenTextures(1, &textureID);

    int width, height, nrComponents;
    unsigned char *data = stbi_load(path.c_str(), &width, &height, &nrComponents, 0);
    if (data)
    {
        GLenum format;
        if (nrComponents == 1)
            format = GL_RED;
        else if (nrComponents == 3)
            format = GL_RGB;
        else if (nrComponents == 4)
            format = GL_RGBA;
        else
            format = GL_RED;

        glBindTexture(GL_TEXTURE_2D, textureID);
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        stbi_image_free(data);
    }
    else
    {
        std::cout << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }

    return textureID;
}