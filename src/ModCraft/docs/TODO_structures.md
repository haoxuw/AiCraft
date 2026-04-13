in @src/shared/entity.h 
enum class EntityKind {
	Living,
	Item,
};


  we want to add another Kind, called Structures -> Including abstract things that made of blocks
       
  Chest (tied to InventoryId and inventory registry)      
  Bed        
  Tree     
  House      
  Spawning etc              
             
  and we can define them in @src/shared/constants.h too 
             
  those Entities are special, because they are made of one or many blocks, and only is still that      
  structure if all the blocks of it's space are still valid.           
   
  Like, if you break a block from the tree, or a wall from the House, then it's no longer have        
  isStructureComplete() == true            
             
  Later we will implement some more interesting game features. For example  only a complete Tree can   
  spawn fallen leaves on the ground etc 