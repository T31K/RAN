


-- sp_purchase_insert_item
CREATE  Procedure [dbo].[sp_purchase_insert_item]
	/* Param List */	
	@purkey varchar(21),
	@useruid varchar(6),
    @productnum int,
    @purprice int,
	@nReturn int OUTPUT
AS
	DECLARE 	
		@error_var int -- Declare variables used in error checking.

	SET NOCOUNT ON

	INSERT INTO ShopPurchase (PurKey, UserUID, ProductNum, PurPrice) VALUES (@purkey, @useruid, @productnum, @purprice)

	SELECT @error_var = @@ERROR
	IF @error_var <> 0 
	BEGIN
	    -- 火涝角菩
	    SET @nReturn = 0	    
	END
        ELSE
        BEGIN
	    -- 沥惑利栏肺 火涝 己傍
	    SET @nReturn = 1
        END

	SET NOCOUNT OFF

	RETURN @nReturn	



