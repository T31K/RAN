USE RanShop;
DROP VIEW IF EXISTS viewShopPurchase;
CREATE VIEW viewShopPurchase AS SELECT   ShopPurchase.PurKey, ShopPurchase.UserUID, 
         ShopPurchase.ProductNum, ShopPurchase.PurPrice, 
         ShopPurchase.PurFlag, ShopPurchase.PurDate, 
         ShopPurchase.PurChgDate, ShopItemMap.ItemMain, 
         ShopItemMap.ItemSub, ShopItemMap.ItemName, 
         ShopPurFlag.PurFlagName
FROM     ShopPurchase INNER JOIN
         ShopItemMap ON 
         ShopPurchase.ProductNum = ShopItemMap.ProductNum LEFT OUTER JOIN
         ShopPurFlag ON ShopPurchase.PurFlag = ShopPurFlag.PurFlag;
DROP VIEW IF EXISTS viewShopPurchaseItem;
CREATE VIEW viewShopPurchaseItem AS SELECT SUM(A.PurPrice) AS tPrice, A.ItemMain, A.ItemSub, B.ItemName
FROM   viewShopPurchase A INNER JOIN
       ShopItemMap B ON A.ProductNum = B.ProductNum
GROUP BY A.ItemMain, A.ItemSub, B.ItemName
ORDER BY A.ItemMain, A.ItemSub;
