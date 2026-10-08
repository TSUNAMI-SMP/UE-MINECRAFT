#pragma once
#include <algorithm>
#include <string>
#include <vector>

// Independent of Unreal: datapack ingredient alternatives, translation/mirroring,
// and bipartite shapeless matching. Greedy matching loses valid tag recipes.
namespace BridgeRecipeMath {
using Ingredient=std::vector<std::string>;
struct Recipe {std::string id,type,result;int width=0,height=0,count=1,ticks=200;std::vector<Ingredient> ingredients;};
inline bool Accepts(const Ingredient& ingredient,const std::string& item) {
    return ingredient.empty() ? item.empty() : (!item.empty() && std::find(ingredient.begin(),ingredient.end(),item)!=ingredient.end());
}
inline bool Assign(const Recipe& recipe,const std::vector<std::string>& items,int index,std::vector<bool>& used) {
    if(index==int(items.size())) return true;
    for(int i=0;i<int(recipe.ingredients.size());++i) if(!used[i] && Accepts(recipe.ingredients[i],items[index])) {
        used[i]=true;if(Assign(recipe,items,index+1,used)) return true;used[i]=false;
    }
    return false;
}
inline bool Matches(const Recipe& recipe,const std::vector<std::string>& grid,int width,int height) {
    if(width<1 || height<1 || width>3 || height>3 || grid.size()!=size_t(width*height) || recipe.result.empty() || recipe.count<1) return false;
    if(recipe.type=="minecraft:crafting_shapeless") {
        std::vector<std::string> items;for(const auto& item:grid) if(!item.empty()) items.push_back(item);
        if(items.empty() || items.size()!=recipe.ingredients.size() || items.size()>9) return false;
        std::vector<bool> used(items.size());return Assign(recipe,items,0,used);
    }
    if(recipe.type!="minecraft:crafting_shaped" || recipe.width<1 || recipe.height<1 || recipe.width>width || recipe.height>height
        || recipe.ingredients.size()!=size_t(recipe.width*recipe.height)) return false;
    for(int oy=0;oy<=height-recipe.height;++oy) for(int ox=0;ox<=width-recipe.width;++ox) for(bool mirror:{false,true}) {
        bool match=true;
        for(int y=0;y<height && match;++y) for(int x=0;x<width;++x) {
            int rx=x-ox,ry=y-oy;
            if(rx<0 || ry<0 || rx>=recipe.width || ry>=recipe.height) {if(!grid[x+y*width].empty()) {match=false;break;}}
            else if(!Accepts(recipe.ingredients[(mirror ? recipe.width-rx-1 : rx)+ry*recipe.width],grid[x+y*width])) {match=false;break;}
        }
        if(match) return true;
    }
    return false;
}
inline bool SingleMatches(const Recipe& recipe,const std::string& input,const std::string& type) {
    return !recipe.result.empty() && recipe.type==type && recipe.ingredients.size()==1 && Accepts(recipe.ingredients[0],input);
}
}
